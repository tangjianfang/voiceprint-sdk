cmake_minimum_required(VERSION 3.20)
project(voiceprint-sdk VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# AVX2 optimization for MSVC
if(MSVC)
    add_compile_options(/arch:AVX2 /utf-8)
endif()

# Include dependency download scripts
include(cmake/download_deps.cmake)
include(cmake/download_models.cmake)

# Internal static library (for DLL + tests to link against)
file(GLOB_RECURSE CORE_SOURCES
    src/core/*.cpp
    src/storage/*.cpp
    src/manager/*.cpp
    src/utils/*.cpp
)

add_library(voiceprint_core STATIC ${CORE_SOURCES})
target_compile_definitions(voiceprint_core PUBLIC NOMINMAX)
target_include_directories(voiceprint_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/kaldi-native-fbank
)
target_link_libraries(voiceprint_core PUBLIC
    onnxruntime
    kaldi-native-fbank-core
    sqlite3
    spdlog::spdlog
)

# Main SDK library (DLL) - thin wrapper around core
add_library(voiceprint SHARED src/api/voiceprint_dll.cpp)
target_compile_definitions(voiceprint PRIVATE VOICEPRINT_EXPORTS)
target_link_libraries(voiceprint PRIVATE voiceprint_core)

# Copy ONNX Runtime DLLs to output directory
add_custom_command(TARGET voiceprint POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:voiceprint>
)

# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()

    # Use pre-downloaded googletest
    set(GTEST_DIR "${CMAKE_SOURCE_DIR}/third_party/googletest")
    if(NOT EXISTS "${GTEST_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "GoogleTest not found at ${GTEST_DIR}")
    endif()
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(${GTEST_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)

    # Unit tests
    file(GLOB_RECURSE TEST_SOURCES tests/unit/*.cpp)
    if(TEST_SOURCES)
        add_executable(unit_tests ${TEST_SOURCES})
        target_link_libraries(unit_tests PRIVATE voiceprint_core GTest::gtest_main)
        add_test(NAME UnitTests COMMAND unit_tests)
    endif()

    # Integration tests
    file(GLOB_RECURSE INTEGRATION_TEST_SOURCES tests/integration/*.cpp)
    if(INTEGRATION_TEST_SOURCES)
        add_executable(integration_tests ${INTEGRATION_TEST_SOURCES})
        target_link_libraries(integration_tests PRIVATE voiceprint GTest::gtest_main)
        target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
        add_test(NAME IntegrationTests COMMAND integration_tests)
    endif()

    # Benchmark tests
    file(GLOB_RECURSE BENCHMARK_SOURCES tests/benchmark/*.cpp)
    if(BENCHMARK_SOURCES)
        add_executable(benchmark_tests ${BENCHMARK_SOURCES})
        target_link_libraries(benchmark_tests PRIVATE voiceprint)
        target_include_directories(benchmark_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    endif()

    # Evaluation tests
    file(GLOB_RECURSE EVAL_SOURCES tests/evaluation/*.cpp)
    if(EVAL_SOURCES)
        add_executable(evaluation_tests ${EVAL_SOURCES})
        target_link_libraries(evaluation_tests PRIVATE voiceprint)
        target_include_directories(evaluation_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    endif()
endif()

# Examples
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/cpp_demo/main.cpp)
        add_executable(cpp_demo examples/cpp_demo/main.cpp)
        target_link_libraries(cpp_demo PRIVATE voiceprint)
        target_include_directories(cpp_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    endif()
endif()
