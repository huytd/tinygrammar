cmake_minimum_required(VERSION 3.16)
project(tinygrammar C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(X11 x11)

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
    ${GTK3_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    src
)

target_compile_options(tinygrammar PRIVATE
    ${GTK3_CFLAGS_OTHER}
    ${X11_CFLAGS_OTHER}
)

target_link_libraries(tinygrammar PRIVATE
    onnxruntime::onnxruntime
    ${GTK3_LIBRARIES}
    ${X11_LIBRARIES}
    ${CMAKE_DL_LIBS}
    pthread
    m
)

set_target_properties(tinygrammar PROPERTIES
    BUILD_RPATH  "${ORT_ROOT}/lib"
    INSTALL_RPATH "${ORT_ROOT}/lib"
)
