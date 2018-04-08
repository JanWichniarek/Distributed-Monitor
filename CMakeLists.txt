cmake_minimum_required(VERSION 3.9)
project(DistributedMonitor)

set(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
find_package(MPI REQUIRED)
include_directories(${MPI_INCLUDE_PATH})

SET(CMAKE_C_COMPILER mpicc)
SET(CMAKE_CXX_COMPILER mpicxx)

add_executable(DistributedMonitor main.cpp DistributedMonitor.cpp DistributedMonitor.h)

