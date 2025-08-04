#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <iostream>

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

    t.join();
    return 0;
}