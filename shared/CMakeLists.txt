add_library(rmm_shared STATIC
        src/protocol.cpp
)

target_include_directories(rmm_shared
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rmm_shared PUBLIC cxx_std_20)