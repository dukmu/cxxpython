cmake_minimum_required(VERSION 3.22)

project(cxxpython LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cxxpython_core
    Objects/runtime.cpp
    Objects/object.cpp
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

add_executable(test_core_objects Tests/cpp/test_core_objects.cpp)
target_link_libraries(test_core_objects PRIVATE cxxpython_core)
target_compile_options(test_core_objects PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME core_objects COMMAND test_core_objects)

