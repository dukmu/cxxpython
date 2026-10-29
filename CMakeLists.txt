cmake_minimum_required(VERSION 3.22)

project(cxxpython LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_path(BOOST_MULTIPRECISION_INCLUDE_DIR boost/multiprecision/cpp_int.hpp)
if(NOT BOOST_MULTIPRECISION_INCLUDE_DIR)
    message(FATAL_ERROR "Boost.Multiprecision headers are required; set BOOST_MULTIPRECISION_INCLUDE_DIR")
endif()

add_library(cxxpython_foundation
    Python/runtime/foundation.cpp
    Objects/core/type.cpp
    Objects/core/objects.cpp
    Objects/core/call.cpp
    Objects/core/descriptor.cpp
    Objects/core/construction.cpp
    Objects/core/number.cpp
    Objects/core/protocol.cpp
    Objects/core/representation.cpp
    Python/execution/code.cpp
    Python/execution/eval.cpp
    Python/execution/traceback.cpp
    Python/execution/unwind.cpp
)
target_include_directories(cxxpython_foundation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(cxxpython_foundation SYSTEM PRIVATE ${BOOST_MULTIPRECISION_INCLUDE_DIR})
target_link_libraries(cxxpython_foundation PUBLIC Threads::Threads)
target_compile_options(cxxpython_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)

option(CXXPYTHON_BUILD_BENCHMARKS "Build foundation cost probes" OFF)
if(CXXPYTHON_BUILD_BENCHMARKS)
    add_executable(bench_foundation Tools/bench_foundation.cpp)
    target_link_libraries(bench_foundation PRIVATE cxxpython_foundation)
    target_compile_options(bench_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(CXXPYTHON_BUILD_LEGACY "Build the legacy interpreter for migration comparisons" ON)
if(CXXPYTHON_BUILD_LEGACY)
add_library(cxxpython_core
    Parser/lexer.cpp
    Parser/parser.cpp
    Python/runtime_bootstrap.cpp
    Python/runtime_accessors.cpp
    Python/runtime_factories.cpp
    Python/runtime_roots.cpp
    Python/runtime_type_descriptors.cpp
    Python/runtime_execution.cpp
    Python/import.cpp
    Python/import_find.cpp
    Python/import_importlib.cpp
    Python/import_load.cpp
    Python/import_extension.cpp
    Python/gc.cpp
    Python/bytecode.cpp
    Python/compiler_symbols.cpp
    Python/compiler.cpp
    Python/vm_fastpaths.cpp
    Python/vm.cpp
    Modules/builtins/builtins.cpp
    Modules/builtins/core.cpp
    Modules/builtins/reflection.cpp
    Modules/builtins/exec.cpp
    Modules/builtins/import.cpp
    Modules/config.cpp
    Modules/sys.cpp
    Modules/gc.cpp
    Modules/posix.cpp
    Modules/signal.cpp
    Modules/time.cpp
    Modules/_io.cpp
    Modules/_collections.cpp
    Modules/_re.cpp
    Modules/_thread.cpp
    Modules/_tokenize.cpp
    Objects/object_core.cpp
    Objects/object_primitives.cpp
    Objects/object_sequences.cpp
    Objects/object_slice.cpp
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
    Objects/object_traceback.cpp
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
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wno-pedantic>
)
target_compile_definitions(cxxpython_core PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:CXXPYTHON_HAS_COMPUTED_GOTO=1>
)
target_link_libraries(cxxpython_core PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
target_compile_definitions(cxxpython_core PRIVATE
    CXXPYTHON_STDLIB_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/Lib\"
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

endif()

if(BUILD_TESTING)
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

add_executable(test_foundation
    Tests/cpp/core/test_memory.cpp
    Tests/cpp/core/test_types.cpp
    Tests/cpp/core/test_call.cpp
    Tests/cpp/core/test_attributes.cpp
    Tests/cpp/core/test_construction.cpp
    Tests/cpp/core/test_layout.cpp
    Tests/cpp/core/test_cell.cpp
    Tests/cpp/core/test_readiness.cpp
    Tests/cpp/core/test_protocol.cpp
    Tests/cpp/core/test_number.cpp
    Tests/cpp/core/test_representation.cpp
    Tests/cpp/core/test_execution.cpp
    Tests/cpp/core/test_scheduling.cpp
    Tests/cpp/core/test_traceback.cpp
    Tests/cpp/core/test_unwind.cpp
    Tests/cpp/core/test_with.cpp
)
target_link_libraries(test_foundation PRIVATE cxxpython_foundation GTest::gtest_main)
target_compile_options(test_foundation PRIVATE -Wall -Wextra -Wpedantic -Werror)
gtest_discover_tests(test_foundation PROPERTIES TIMEOUT 15)

if(CXXPYTHON_BUILD_LEGACY)
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
    CXXPYTHON_SAMPLE_EXTENSION_PATH=\"$<TARGET_FILE:cxxpython_sample_extension>\"
    CXXPYTHON_FAILING_EXTENSION_PATH=\"$<TARGET_FILE:cxxpython_failing_extension>\"
)
target_compile_options(test_core_objects PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

gtest_discover_tests(test_core_objects)

add_library(cxxpython_sample_extension MODULE Tests/extensions/cxxpython_sample_extension.c)
target_include_directories(cxxpython_sample_extension
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)
set_target_properties(cxxpython_sample_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cxxpython_sample_extension"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions"
)

add_library(cxxpython_failing_extension MODULE Tests/extensions/cxxpython_failing_extension.c)
target_include_directories(cxxpython_failing_extension
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Include
)
set_target_properties(cxxpython_failing_extension PROPERTIES
    PREFIX ""
    OUTPUT_NAME "cxxpython_failing_extension"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/extensions"
)
endif()
endif()
