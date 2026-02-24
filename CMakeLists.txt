cmake_minimum_required(VERSION 3.25)

set(USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(protoc-gen-odin)

find_package(protobuf CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# NOTE: there seems to be an issue with PROTOC_OUT_DIR in protobuf_generate where it
# refuses to write to that actual dir, and uses the build dir instead (base of output dir perhaps),
# hence we add the build folder as an include (as opposed to <build>/generated).
# See https://github.com/protocolbuffers/protobuf/issues/12450
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_library(proto-objects OBJECT "${CMAKE_CURRENT_LIST_DIR}/protobuf/options.proto")
target_include_directories(proto-objects PUBLIC "$<BUILD_INTERFACE:${PROTO_BINARY_DIR}>")

protobuf_generate(
    TARGET proto-objects
    IMPORT_DIRS "${CMAKE_CURRENT_LIST_DIR}/protobuf"
    PROTOC_OUT_DIR "${CMAKE_BINARY_DIR}")

set(HEADER_FILES
    source/pch.h
    source/odin-code-generator.h)

set(SOURCE_FILES
    source/main.cpp
    source/odin-code-generator.cpp)

add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

target_link_libraries(${PROJECT_NAME} 
    proto-objects
    protobuf::libprotobuf
    protobuf::libprotoc
    fmt::fmt)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_FILES} ${HEADER_FILES})

target_precompile_headers(${PROJECT_NAME} PUBLIC "source/pch.h")

install(TARGETS ${PROJECT_NAME})

set(CPACK_PACKAGE_NAME $ENV{PACKAGE_NAME})
set(CPACK_PACKAGE_VERSION $ENV{PACKAGE_VERSION})
set(CPACK_SYSTEM_NAME $ENV{PACKAGE_SYSTEM})
include(CPack)
