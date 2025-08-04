# Specify the minimum version of CMake required to build the project.
# Version 3.10 is a good modern baseline.
cmake_minimum_required(VERSION 3.10)

# Set the project name and programming languages used.
project(SPSC_CMAKE_PROJECT LANGUAGES CXX)

# tells the compiler to use the C++17 language standard for the executable target
set(CMAKE_CXX_STANDARD 17)
# makes C++17 a hard requirement
set(CMAKE_CXX_STANDARD_REQUIRED True)
# helps guarantees that project will compile with any C++17-compliant compiler
set(CMAKE_CXX_EXTENSIONS False)

# find the Threads library, required for std::thread
find_package(Threads REQUIRED)

# Add an executable target. First argument is the name of the executable
# that will be created, and the second is the source file
add_executable(spsc_app main.cpp)

# link the Threads library to the executable defined above
target_link_libraries(spsc_app PRIVATE Threads::Threads)