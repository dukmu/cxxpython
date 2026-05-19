cmake_minimum_required(VERSION 3.22)

project(cxxpython LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cxxpython_core
    Parser/lexer.cpp
    Parser/parser.cpp
    Python/bytecode.cpp
    Python/compiler.cpp
    Python/vm.cpp
    Objects/runtime_bootstrap.cpp
    Objects/runtime_builtins.cpp
    Objects/runtime_accessors.cpp
    Objects/runtime_factories.cpp
    Objects/runtime_import.cpp
    Objects/runtime_roots.cpp
    Objects/object_core.cpp
    Objects/object_primitives.cpp
    Objects/object_sequences.cpp
    Objects/object_mapping.cpp
    Objects/object_iterator.cpp
    Objects/object_iterables.cpp
    Objects/object_module.cpp
    Objects/object_native_function.cpp
    Objects/object_function.cpp
    Objects/object_cell.cpp
    Objects/object_super.cpp
    Objects/object_descriptor.cpp
    Objects/object_descriptor_family.cpp
    Objects/object_property.cpp
    Objects/object_method.cpp
    Objects/object_slot_wrapper.cpp
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

find_path(READLINE_INCLUDE_DIR readline/readline.h)
find_library(READLINE_LIBRARY NAMES readline)
find_library(HISTORY_LIBRARY NAMES history)
if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
    target_compile_definitions(cxxpython PRIVATE CXXPYTHON_HAS_READLINE=1)
    target_include_directories(cxxpython PRIVATE ${READLINE_INCLUDE_DIR})
    target_link_libraries(cxxpython PRIVATE ${READLINE_LIBRARY})
    if(HISTORY_LIBRARY)
        target_link_libraries(cxxpython PRIVATE ${HISTORY_LIBRARY})
    endif()
endif()

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
    Tests/cpp/test_functions.cpp
    Tests/cpp/test_attributes.cpp
    Tests/cpp/test_module.cpp
    Tests/cpp/test_bytecode.cpp
    Tests/cpp/test_compile.cpp
    Tests/cpp/test_cli.cpp
    Tests/cpp/test_python_scripts.cpp
)
target_link_libraries(test_core_objects PRIVATE cxxpython_core GTest::gtest_main)
target_compile_definitions(test_core_objects PRIVATE
    CXXPYTHON_PROGRAM_PATH=\"$<TARGET_FILE:cxxpython>\"
    CXXPYTHON_PYTHON_TEST_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Tests/python\"
)
target_compile_options(test_core_objects PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

gtest_discover_tests(test_core_objects)
