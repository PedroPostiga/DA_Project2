cmake_minimum_required(VERSION 3.22)
project(DA_Project2)

set(CMAKE_CXX_STANDARD 20)

add_executable(DA_Project2
        include/DataTypes.h
        include/Parser.h
        src/Parser.cpp
        include/InterferenceGraph.h
        src/InterferenceGraph.cpp
        include/Graph.h
        include/RegisterAllocator.h
        src/RegisterAllocator.cpp)

target_include_directories(DA_Project2 PRIVATE include)