cmake_minimum_required(VERSION 3.15)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  MESSAGE(FATAL_ERROR "Toolchain file not set, try using: cmake --preset default")
endif()

project (SilverDOM)

find_package(unofficial-skia CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory("src/allocator")
add_subdirectory("src/windows")
