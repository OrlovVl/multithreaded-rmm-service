find_package(GTest CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(rmm_server_tests
        server/test_protocol.cpp
        server/test_auth_service.cpp
)

target_include_directories(rmm_server_tests PRIVATE ${CMAKE_SOURCE_DIR}/server/include ${CMAKE_SOURCE_DIR}/shared/include)
target_link_libraries(rmm_server_tests PRIVATE rmm_shared GTest::gtest GTest::gtest_main Boost::system SQLite::SQLite3)
target_compile_features(rmm_server_tests PRIVATE cxx_std_20)

add_test(NAME rmm_server_tests COMMAND rmm_server_tests)

add_executable(rmm_client_tests
        client/test_local_metrics_store.cpp
)

target_include_directories(rmm_client_tests PRIVATE ${CMAKE_SOURCE_DIR}/client/include ${CMAKE_SOURCE_DIR}/shared/include)
target_link_libraries(rmm_client_tests PRIVATE rmm_shared Qt6::Test SQLite::SQLite3)
target_compile_features(rmm_client_tests PRIVATE cxx_std_20)

add_test(NAME rmm_client_tests COMMAND rmm_client_tests)