#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <atomic>

struct Msg{
    float arrayOfNumbers[8];
    bool keepRunning;
};

//here there is an interesting command 
static_assert(std::is_trivially_copyable_v<Msg>,"msg must be trivial");
// this is not a function, but is a "trait" it answers " may i copy the object bit for bit with memcpy, move it in shared memory, or read it from 
//unitialized storage without running any user defined constructors, desctors or copy/move functions! interesting!"

struct Mailbox {
    // this holds the messages
    Msg slots[2];

    alignas(64) std::atomic<int> latest_idx{0};
};

void send(Mailbox &mb, const Msg &msg) {
    // finds the slot that is NOT being read from
    const int current_idx = mb.latest_idx.load(std::memory_order_relaxed);
    const int write_idx = 1 - current_idx;

    // write the new data into this the currently unused slot
    mb.slots[write_idx] = msg;

    // publish the new data. 'release' memory order ensures that the write to the slot above is 100% complete before the index is
    // updated, preventing torn reads
    mb.latest_idx.store(write_idx, std::memory_order_release);
}

/**
 * @brief Safely peeks at the latest message in the mailbox.
 * @param mb The mailbox to peek from.
 * @return A copy of the latest, complete message.
 */
Msg peek(Mailbox &mb) {
    // Atomically load the index. The 'acquire' memory order syncs with the
    // 'release' in the send function, guaranteeing we only see the index
    // *after* the message write is complete
    // The reason why we need this and we cant just atomically store the message, is because the msg is a "complex struct"
    // std::atomic is only guaranteed to work on single primitive types. Msg is too complex of a struct to read and write to quickly
    // It would have to copy a piece of data little by little that could lead to a torn msg
    const int read_idx = mb.latest_idx.load(std::memory_order_acquire);

    // Return a copy of the data from the slot that is now safe to read.
    return mb.slots[read_idx];
}

struct alignas(64) Ring{
    //alignas(64) requests that the compiler place the start of the struct on a 64 
    //byte boundary where 64B is one cache line (smallest unit of data that can be transferred between the CPU cache and main memory) on x86
    std::atomic<size_t> head{0};
    //index one past the last commited write, starts at 0
    std::atomic<size_t> tail{0};
    //index of the next item to read (consumer)
    Msg buf[16]; //the fixed size circular buffer of payload objects
    // power of two length
};

bool try_push(Ring& q, const Msg& m) {
    // q.head is a std::atomic, so we must use .load() to access it safely when other threads might also be using it
    size_t h = q.head.load(std::memory_order_relaxed);
    size_t t = q.tail.load(std::memory_order_acquire); 
    if (h-t == std::size(q.buf)) // full 
        return false; 
    q.buf[h & 15] = m; 
    q.head.store(h+1, std::memory_order_release); 
    return true;
}

bool try_pop(Ring& q, Msg& out){
    size_t t = q.tail.load(std::memory_order_relaxed); 
    size_t h = q.head.load(std::memory_order_acquire);
    if (t==h){ //empty
        return false;
    }
    out = q.buf[t & 15];
    q.tail.store(t+1,std::memory_order_release);
    return true;
}

// This is the RT thread, it writes the messages that the observer is meant to read x5 as slow
void continuousThreadFunction(Ring& tx, Mailbox& mb){
    int i= 0;
    auto wake_up = std::chrono::high_resolution_clock::now();

    while(true) {
        wake_up += std::chrono::milliseconds(20);
        i+=1;

        // peek what new command was sent from observer
        Msg command = peek(mb);

        // If observer tells you to keep going, keep going
        if (!command.keepRunning) {
            break;
        }

        // 
        // std::cout << "Hello from new thread!" << std::endl;
        Msg msg = {};
        msg.keepRunning = true;
        msg.arrayOfNumbers[0] = command.arrayOfNumbers[0] + static_cast<float>(i);

        // for(int j = 0; j < 8; j++){
        //     msg.arrayOfNumbers[j] = i;
        // }
        try_push(tx, msg);
        printf("  RT Thread Pushed:  %f\n", msg.arrayOfNumbers[0]);

        // if (i > 30){
        //     msg.keepRunning = false;
        //     break; 
        // }     
        // try_push(tx, msg);  
        std::this_thread::sleep_until(wake_up);
    }
}

// This is the observer thread, waits for the msgs and reads them five times as slow
int main() {
    printf("hello world\n");

    // These are what actually hold the data that are being read and written to
    Ring rtToMain;
    Mailbox mainToRT;

    Msg command = {};
    command.keepRunning = true;
    command.arrayOfNumbers[0] = 0.0f;
    send(mainToRT, command);


    std::thread t(continuousThreadFunction, std::ref(rtToMain), std::ref(mainToRT));


    //here, thread is a class that represents a thread of execution 
    //t is an instance of the thread class, and threadFucntion is the parameter to the constructor
    // t.join(); if we use t.join then we wait for that thread to finish before moving on 
    // t.detach(); //this lets it run independently

    // //now that we have the continous thread running, we can begin trying to communicate between 
    // //the two threads using a publisher subscriber model. we will use this main thread as the other 
    // //thread 
    // //we will start with the communication from the "real time" simulated thread to the main thread, where 
    // //the real time thread will run at a faster speed than this main thread. 
    // while(true) {
    //     Msg recieve;
    //     printf("draining queue \n");
    //     while (try_pop(rtToMain,recieve)){
    //         printf("keepRunning? %d \n", recieve.keepRunning);
    //         printf("i %f \n", recieve.arrayOfNumbers[0]);
    //     }
    //     // printf("message from main thread");
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // }

    auto wake_up = std::chrono::high_resolution_clock::now();

    // Loop a few times, sending a new command each time
    for (int i = 1; i <= 4; ++i) {
        wake_up += std::chrono::milliseconds(100);
        printf("\n--- Observer Loop %d ---\n", i);

        // Set a new command value to send
        command.arrayOfNumbers[0] = static_cast<float>(i * 100);
        printf("Observer sending new command: %f\n", command.arrayOfNumbers[0]);
        send(mainToRT, command);

        // Wait a second to let the RT thread run
        std::this_thread::sleep_until(wake_up);

        // now drain the rt queue to see what the RT thread produced
        Msg msg;
        printf("Observer reading from RT queue:\n");
        while (try_pop(rtToMain, msg)) {
            printf("  > Popped RT values: %f\n", msg.arrayOfNumbers[0]);
        }
    }

    // tells real-time thread to shut down
    printf("\nObserver sending shutdown command...\n");
    command.keepRunning = false;
    send(mainToRT, command);

    // wait for the thread to finish
    t.join();
    printf("done \n");

    return 0;
}