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



struct alignas(64) Ring{
    //alignas(64) requests that the compiler place the start of the struct on a 64 
    //byte boundary where 64B is one cache line on x86
    std::atomic<size_t> head{0};
    //index one past the last commited write, starts at 0
    std::atomic<size_t> tail{0};
    //index of the next item to read (consumer)
    Msg buf[4]; //the fixed size circular buffer of payload objects
    //powewr of two length
};

bool try_push(Ring& q, const Msg& m){
    size_t h = q.head.load(std::memory_order_relaxed); 
    size_t t = q.tail.load(std::memory_order_acquire); 
    if (h-t == std::size(q.buf)) //full 
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

void continuousThreadFunction(Ring& tx){
    int i= 0;
    while(true){
        i+=1;
        // std::cout << "Hello from new thread!" << std::endl;
        Msg m;
        for(int j = 0; j < 4; j++){
            m.arrayOfNumbers[j] = i;
        }
        m.keepRunning = true;
        if (i > 30){
            m.keepRunning = false;
            break; 
        }     
        try_push(tx,m);  
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int main(){
    printf("hello world\n");
    Ring rtToMain;


    std::thread t(continuousThreadFunction,std::ref(rtToMain));


    //here, thread is a class that represents a thread of execution 
    //t is an instance of the thread class, and threadFucntion is the parameter to the constructor
    // t.join(); if we use t.join then we wait for that thread to finish before moving on 
    t.detach(); //this lets it run independently

    //now that we have the continous thread running, we can begin trying to communicate between 
    //the two threads using a publisher subscriber model. we will use this main thread as the other 
    //thread 
    //we will start with the communication from the "real time" simulated thread to the main thread, where 
    //the real time thread will run at a faster speed than this main thread. 
    while(true){
        Msg recieve;
        printf("draining queue \n");
        while (try_pop(rtToMain,recieve)){
            printf("keepRunning? %d \n", recieve.keepRunning);
            printf("i %f \n", recieve.arrayOfNumbers[0]);
        }
        // printf("message from main thread");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    }
    printf("done \n");

    return 0;
}