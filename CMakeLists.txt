cmake_minimum_required(VERSION 3.1)

project(myers-diff)

set(CMAKE_CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

add_executable(diff diff.cpp)
