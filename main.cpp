#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <iostream>

struct Msg{
    uint64_t curtime; 
    float position_example[8];
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
    Msg buf[16]; //the fixed size circular buffer of payload objects
    //powewr of two length
};



void continuousThreadFunction(){
    while(true){
        std::cout << "Hello from new thread!" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main(){
    printf("hello world\n");


    std::thread t(continuousThreadFunction);


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
        printf("message from main thread");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    }

    return 0;
}