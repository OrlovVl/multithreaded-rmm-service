cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0167 NEW)

project(rmm_service VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build tests" ON)

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()