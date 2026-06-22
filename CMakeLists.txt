cmake_minimum_required(VERSION 3.16)
project(tinygrammar C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ONNXRuntime
set(ORT_ROOT ${CMAKE_SOURCE_DIR}/external/onnxruntime)

add_library(onnxruntime::onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION             "${ORT_ROOT}/lib/libonnxruntime.so"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include"
)

file(GLOB_RECURSE SRC_FILES src/*.c)
add_executable(tinygrammar ${SRC_FILES})

target_include_directories(tinygrammar PRIVATE
    ${SPM_ROOT}/src
    src
)

target_link_libraries(tinygrammar PRIVATE
    onnxruntime::onnxruntime
)

set_target_properties(tinygrammar PROPERTIES
    BUILD_RPATH  "${ORT_ROOT}/lib"
    INSTALL_RPATH "${ORT_ROOT}/lib"
)
