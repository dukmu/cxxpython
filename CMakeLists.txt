cmake_minimum_required(VERSION 3.22)
project(cxxpython LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_path(BOOST_MULTIPRECISION_INCLUDE_DIR boost/multiprecision/cpp_int.hpp)
if(NOT BOOST_MULTIPRECISION_INCLUDE_DIR)
    message(FATAL_ERROR "Boost.Multiprecision headers are required; set BOOST_MULTIPRECISION_INCLUDE_DIR")
endif()

add_library(cxxpython_core
    Python/gc.cpp
    Python/compile.cpp
    Python/symtable.cpp
    Python/flowgraph.cpp
    Python/ceval.cpp
    Python/traceback.cpp
    Python/unwind.cpp
    Parser/parser.cpp
    Objects/typeobject.cpp
    Objects/object.cpp
    Objects/call.cpp
    Objects/descrobject.cpp
    Objects/typeconstruction.cpp
    Objects/longobject.cpp
    Objects/rangeobject.cpp
    Objects/iterobject.cpp
    Objects/abstract.cpp
    Objects/representation.cpp
)
target_include_directories(cxxpython_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(cxxpython_core SYSTEM PRIVATE ${BOOST_MULTIPRECISION_INCLUDE_DIR})
target_link_libraries(cxxpython_core PUBLIC Threads::Threads)
target_compile_options(cxxpython_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(cxxpython Programs/cxxpython.cpp)
target_link_libraries(cxxpython PRIVATE cxxpython_core)
target_compile_options(cxxpython PRIVATE -Wall -Wextra -Wpedantic -Werror)

option(CXXPYTHON_BUILD_BENCHMARKS "Build runtime cost and parallel scaling probes" OFF)
if(CXXPYTHON_BUILD_BENCHMARKS)
    add_executable(bench_runtime Tools/benchmarks/runtime.cpp)
    target_link_libraries(bench_runtime PRIVATE cxxpython_core)
    target_compile_options(bench_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format-14 clang-format REQUIRED)
file(GLOB_RECURSE FORMAT_SOURCES CONFIGURE_DEPENDS
    Include/*.hpp Objects/*.cpp Parser/*.cpp Python/*.cpp Programs/*.cpp
    Tests/cpp/*.cpp Tools/benchmarks/*.cpp)
add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i --style=file ${FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Format project C++ sources with clang-format"
    VERBATIM)
add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror --style=file ${FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Check project C++ formatting"
    VERBATIM)

if(BUILD_TESTING)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
            URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7)
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)

    add_executable(test_runtime
        Tests/cpp/test_memory.cpp
        Tests/cpp/test_gc_epochs.cpp
        Tests/cpp/test_parallel_vm.cpp
        Tests/cpp/test_types.cpp
        Tests/cpp/test_call.cpp
        Tests/cpp/test_attributes.cpp
        Tests/cpp/test_construction.cpp
        Tests/cpp/test_layout.cpp
        Tests/cpp/test_cell.cpp
        Tests/cpp/test_readiness.cpp
        Tests/cpp/test_protocol.cpp
        Tests/cpp/test_number.cpp
        Tests/cpp/test_representation.cpp
        Tests/cpp/test_execution.cpp
        Tests/cpp/test_scheduling.cpp
        Tests/cpp/test_traceback.cpp
        Tests/cpp/test_unwind.cpp
        Tests/cpp/test_with.cpp
        Tests/cpp/test_source.cpp
        Tests/cpp/test_iteration.cpp)
    target_link_libraries(test_runtime PRIVATE cxxpython_core GTest::gtest_main)
    target_compile_options(test_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    gtest_discover_tests(test_runtime PROPERTIES TIMEOUT 15)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CXXPYTHON_REFERENCE_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
        "CPython executable used for source differential tests")
    add_test(NAME Source.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Lib/test/test_source.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Source.Differential PROPERTIES TIMEOUT 60)
    add_test(NAME Benchmarks.Report COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${CMAKE_CURRENT_SOURCE_DIR}/Lib/test -p test_benchmarks.py)
    set_tests_properties(Benchmarks.Report PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME CodingStyle.ClangFormat
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror --style=file ${FORMAT_SOURCES})
    set_tests_properties(CodingStyle.ClangFormat PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
endif()
