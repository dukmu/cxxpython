cmake_minimum_required(VERSION 3.22)

project(cxxpython LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cxxpython_core
    Python/bytecode.cpp
    Python/vm.cpp
    Objects/runtime_bootstrap.cpp
    Objects/runtime_accessors.cpp
    Objects/runtime_factories.cpp
    Objects/runtime_roots.cpp
    Objects/object_core.cpp
    Objects/object_primitives.cpp
    Objects/object_sequences.cpp
    Objects/object_mapping.cpp
    Objects/object_exception.cpp
    Objects/object_utils.cpp
)

target_include_directories(cxxpython_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)

target_compile_options(cxxpython_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_executable(cxxpython Programs/cxxpython.cpp)
target_link_libraries(cxxpython PRIVATE cxxpython_core)

enable_testing()

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(test_core_objects
    Tests/cpp/test_bootstrap.cpp
    Tests/cpp/test_sequences.cpp
    Tests/cpp/test_mapping.cpp
    Tests/cpp/test_heap_types.cpp
    Tests/cpp/test_bytecode.cpp
)
target_link_libraries(test_core_objects PRIVATE cxxpython_core GTest::gtest_main)
target_compile_options(test_core_objects PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

gtest_discover_tests(test_core_objects)
