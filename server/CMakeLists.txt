cmake_minimum_required(VERSION 3.15)
project(rmm_server LANGUAGES CXX)

cmake_policy(SET CMP0167 OLD)
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.tar.gz
)
FetchContent_MakeAvailable(googletest)

include_directories(include)

set(SERVER_CORE_SOURCES
        src/models/Metric.cpp
        src/models/ClientInfo.cpp
        src/repository/InMemoryMetricsRepository.cpp
        src/services/AuthService.cpp
        src/services/MetricsService.cpp
        src/net/Session.cpp
        src/net/Server.cpp
)

add_executable(rmm_server ${SERVER_CORE_SOURCES} src/main.cpp)
target_link_libraries(rmm_server PRIVATE Boost::system Threads::Threads)

if(WIN32)
    target_link_libraries(rmm_server PRIVATE ws2_32 mswsock)
endif()

enable_testing()
add_executable(rmm_server_tests ${SERVER_CORE_SOURCES} ../tests/server/test_server.cpp)
target_link_libraries(rmm_server_tests PRIVATE
        Boost::system
        Threads::Threads
        gtest_main
)

if(WIN32)
    target_link_libraries(rmm_server_tests PRIVATE ws2_32 mswsock)
endif()

add_test(NAME rmm_tests COMMAND rmm_server_tests)