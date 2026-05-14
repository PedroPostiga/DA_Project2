cmake_minimum_required(VERSION 3.22)
project(DA_Project2)

set(CMAKE_CXX_STANDARD 20)

add_executable(DA_Project2
        include/DataTypes.h
        src/DataTypes/AlgorithmConfig.cpp
        src/DataTypes/LiveRange.cpp
        src/DataTypes/Web.cpp
        include/Parser.h
        src/Parser/Parser.cpp
        include/InterferenceGraph.h
        src/InterferenceGraph/InterferenceGraph.cpp
        include/Graph.h
        include/RegisterAllocator.h
        src/RegisterAllocator/RegisterAllocator.cpp
        src/RegisterAllocator/AllocationResult.cpp
        src/RegisterAllocator/Helpers.cpp
        src/RegisterAllocator/Algorithms.cpp
        src/main.cpp)

target_include_directories(DA_Project2 PRIVATE include)