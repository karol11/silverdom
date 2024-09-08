cmake_minimum_required(VERSION 3.15)

project (Allocator)

add_library(ag_alloc STATIC
    allocator.c
    allocator.h)
set_property(TARGET ag_alloc PROPERTY C_STANDARD 11)
target_include_directories(ag_alloc  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})    

add_executable(allocator-test
    allocator-test.c)
set_property(TARGET allocator-test PROPERTY C_STANDARD 11)
target_include_directories(allocator-test  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})    
