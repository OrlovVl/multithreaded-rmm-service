find_package(Boost REQUIRED COMPONENTS system property_tree)
find_package(SQLite3 REQUIRED)

add_executable(rmm_server
        src/business/auth_service.cpp
        src/business/session_registry.cpp
        src/data/metric_repository.cpp
        src/network/session.cpp
        src/network/server.cpp
        src/main.cpp
)

target_include_directories(rmm_server
        PRIVATE
        include
)

target_link_libraries(rmm_server
        PRIVATE
        rmm_shared
        Boost::system
        Boost::property_tree
        SQLite::SQLite3
)

target_compile_features(rmm_server PRIVATE cxx_std_20)
target_compile_definitions(rmm_server PRIVATE _WIN32_WINNT=0x0601)