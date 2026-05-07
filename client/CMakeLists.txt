find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test Core)
find_package(SQLite3 REQUIRED)

set(CMAKE_AUTOMOC ON)

add_executable(rmm_client
        src/business/metrics_collector.cpp
        src/business/metrics_json.cpp
        src/business/sync_service.cpp
        src/business/metrics_collector_worker.cpp
        src/data/local_metrics_store.cpp
        src/network/client_channel.cpp
        src/ui/main_window.cpp
        src/main.cpp
        # Заголовки с Q_OBJECT обязательны для AUTOMOC
        include/rmm/client/business/metrics_collector_worker.hpp
        include/rmm/client/business/sync_service.hpp
        include/rmm/client/network/client_channel.hpp
        include/rmm/client/ui/main_window.hpp
)

target_include_directories(rmm_client
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rmm_client
        PRIVATE
        rmm_shared
        Qt6::Widgets
        Qt6::Network
        Qt6::Core
        SQLite::SQLite3
        ole32
        oleaut32
        wbemuuid
)

# Копирование платформенного плагина Qt в папку сборки
add_custom_command(TARGET rmm_client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rmm_client>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/vcpkg/installed/x64-windows/debug/Qt6/plugins/platforms/qwindowsd.dll"
        "$<TARGET_FILE_DIR:rmm_client>/platforms/"
)

target_compile_features(rmm_client PRIVATE cxx_std_20)
target_compile_definitions(rmm_client PRIVATE _WIN32_WINNT=0x0601)