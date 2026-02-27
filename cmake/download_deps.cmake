# download_deps.cmake - Configure all third-party dependencies
# Dependencies are expected to be pre-downloaded to third_party/

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# ============================================================================
# 1. ONNX Runtime - Pre-built package
# ============================================================================
set(ONNXRUNTIME_VERSION "1.17.1")
set(ONNXRUNTIME_ROOT "${THIRD_PARTY_DIR}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}")

if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime not found at ${ONNXRUNTIME_ROOT}.\n"
        "Please download and extract to ${THIRD_PARTY_DIR}/")
endif()

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include"
)

# ============================================================================
# 2. kaldi-native-fbank - Source build
# ============================================================================
set(KNF_DIR "${THIRD_PARTY_DIR}/kaldi-native-fbank")

if(NOT EXISTS "${KNF_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "kaldi-native-fbank not found at ${KNF_DIR}.")
endif()

set(KALDI_NATIVE_FBANK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KALDI_NATIVE_FBANK_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(KALDI_NATIVE_FBANK_ENABLE_CHECK OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${KNF_DIR} ${CMAKE_BINARY_DIR}/kaldi-native-fbank EXCLUDE_FROM_ALL)

# ============================================================================
# 3. SQLite3 - Amalgamation source
# ============================================================================
set(SQLITE_DIR "${THIRD_PARTY_DIR}/sqlite3")

if(NOT EXISTS "${SQLITE_DIR}/sqlite3.c")
    message(FATAL_ERROR "SQLite3 not found at ${SQLITE_DIR}/sqlite3.c.")
endif()

add_library(sqlite3 STATIC ${SQLITE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${SQLITE_DIR})
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_WAL=1
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
endif()

# ============================================================================
# 4. spdlog - Logging library
# ============================================================================
set(SPDLOG_DIR "${THIRD_PARTY_DIR}/spdlog")

if(NOT EXISTS "${SPDLOG_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "spdlog not found at ${SPDLOG_DIR}.")
endif()

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(${SPDLOG_DIR} ${CMAKE_BINARY_DIR}/spdlog EXCLUDE_FROM_ALL)
