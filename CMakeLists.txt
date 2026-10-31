cmake_minimum_required(VERSION 3.22)
project(cxxpython LANGUAGES C CXX)

include(CTest)
set(CXXPYTHON_TEST_TIMEOUT 15 CACHE STRING "Per-native-test timeout; sanitizer builds may raise it")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(Readline QUIET IMPORTED_TARGET readline)
    pkg_check_modules(Ncurses QUIET IMPORTED_TARGET ncursesw)
endif()
add_subdirectory(Parser)
find_path(BOOST_MULTIPRECISION_INCLUDE_DIR boost/multiprecision/cpp_int.hpp)
if(NOT BOOST_MULTIPRECISION_INCLUDE_DIR)
    message(FATAL_ERROR "Boost.Multiprecision headers are required; set BOOST_MULTIPRECISION_INCLUDE_DIR")
endif()

add_library(cxxpython_core
    Python/gc.cpp
    Python/errors.cpp
    Python/pylifecycle.cpp
    Python/pythonrun.cpp
    Python/compile.cpp
    Python/ast_unparse.cpp
    Python/ast.cpp
    Python/bltinmodule.cpp
    Python/builtinexec.cpp
    Python/import.cpp
    Python/extension.cpp
    Modules/_impmodule.cpp
    Modules/_iomodule.cpp
    Modules/_operator.cpp
    Modules/timemodule.cpp
    Modules/_opcodemodule.cpp
    Modules/_tokenizemodule.cpp
    Modules/_astmodule.cpp
    Modules/_stringmodule.cpp
    Modules/_sre/engine.c
    Modules/_sre/sre.cpp
    Objects/srematchobject.cpp
    Objects/srepatternobject.cpp
    Modules/_io/stdio.cpp
    Modules/_io/iobase.cpp
    Modules/_io/bufferedio.cpp
    Modules/_io/textio.cpp
    Modules/_io/open.cpp
    Modules/_io/fileio.cpp
    Modules/_io/bytesio.cpp
    Modules/_io/stringio.cpp
    Modules/_threadmodule.cpp
    Modules/_abc.cpp
    Modules/atexitmodule.cpp
    Modules/itertoolsmodule.cpp
    Modules/itertools_combinatorics.cpp
    Modules/itertools_groups.cpp
    Modules/_collectionsmodule.cpp
    Modules/binascii.cpp
    Modules/main.cpp
    Modules/sysmodule.cpp
    Modules/unicodedatamodule.cpp
    Modules/gcmodule.cpp
    Modules/_weakref.cpp
    Modules/_codecsmodule.cpp
    Modules/_struct.cpp
    Modules/_hashopenssl.cpp
    Modules/_blake2/impl/blake2b-ref.c
    Modules/_blake2/impl/blake2s-ref.c
    Modules/posixmodule.cpp
    Modules/errnomodule.cpp
    Modules/signalmodule.cpp
    Python/symtable.cpp
    Python/flowgraph.cpp
    Python/ceval.cpp
    Python/traceback.cpp
    Python/unwind.cpp
    Parser/ast_lower.cpp
    Parser/ast_public.cpp
    Parser/source.cpp
    Objects/typeobject.cpp
    Objects/object.cpp
    Objects/funcobject.cpp
    Objects/dictobject.cpp
    Objects/dictstorage.cpp
    Objects/listobject.cpp
    Objects/sliceobject.cpp
    Objects/formatobject.cpp
    Objects/stringformat.cpp
    Objects/unicodeobject.cpp
    Objects/bytesobject.cpp
    Objects/bytesmethods.cpp
    Objects/unicodecodecs.cpp
    Objects/superobject.cpp
    Objects/genobject.cpp
    Objects/asyncgenobject.cpp
    Objects/namespaceobject.cpp
    Objects/genericaliasobject.cpp
    Objects/typealiasobject.cpp
    Objects/typevarobject.cpp
    Objects/paramspecobject.cpp
    Modules/_typingmodule.cpp
    Objects/unionobject.cpp
    Objects/weakrefobject.cpp
    Objects/weakproxyobject.cpp
    Objects/setobject.cpp
    Objects/setoperations.cpp
    Objects/moduleobject.cpp
    Objects/call.cpp
    Objects/descrobject.cpp
    Objects/typeconstruction.cpp
    Objects/longobject.cpp
    Objects/floatobject.cpp
    Objects/complexobject.cpp
    Objects/bytearrayobject.cpp
    Objects/memoryobject.cpp
    Objects/memoryformat.cpp
    Objects/exceptions.cpp
    Objects/rangeobject.cpp
    Objects/iterobject.cpp
    Objects/enumobject.cpp
    Objects/aiterobject.cpp
    Objects/exceptiongroup.cpp
    Objects/patternmatching.cpp
    Objects/abstract.cpp
    Objects/representation.cpp
    Objects/reduction.cpp
)
target_include_directories(cxxpython_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)
target_include_directories(cxxpython_core SYSTEM PRIVATE ${BOOST_MULTIPRECISION_INCLUDE_DIR})
set(CXXPYTHON_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Runtime resource prefix (no virtual environment)")
set(CXXPYTHON_STDLIB_DIR "${CXXPYTHON_PREFIX}/Lib" CACHE PATH "Python standard library source directory")
target_compile_definitions(cxxpython_core PRIVATE
    CXXPYTHON_PREFIX="${CXXPYTHON_PREFIX}"
    CXXPYTHON_STDLIB_DIR="${CXXPYTHON_STDLIB_DIR}")
target_link_libraries(cxxpython_core PUBLIC cxxpython_parser Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(cxxpython_core PRIVATE OpenSSL::Crypto)
if(TARGET PkgConfig::Readline)
    target_sources(cxxpython_core PRIVATE Modules/readlinemodule.cpp)
    target_link_libraries(cxxpython_core PRIVATE PkgConfig::Readline)
    target_compile_definitions(cxxpython_core PRIVATE CXXPYTHON_HAVE_READLINE=1)
endif()
if(TARGET PkgConfig::Ncurses)
    target_sources(cxxpython_core PRIVATE Modules/_cursesmodule.cpp)
    target_link_libraries(cxxpython_core PRIVATE PkgConfig::Ncurses)
    target_compile_definitions(cxxpython_core PRIVATE CXXPYTHON_HAVE_CURSES=1)
endif()
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
    Include/*.hpp Include/*.h Objects/*.cpp Parser/*.cpp Python/*.cpp Modules/*.cpp Programs/*.cpp
    Tests/cpp/*.cpp Tests/extensions/*.c Tools/benchmarks/*.cpp Tools/parser/*.cpp)
list(REMOVE_ITEM FORMAT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Parser/generated_parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/cxxpython/grammar.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/internal/unicodedata_db.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/internal/unicodedata_type_db.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Include/internal/unicodedata_sequences.hpp)
list(APPEND FORMAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Modules/_sre/engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/_sre/engine.h)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(regen-parser
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peg_generator/generate.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_custom_target(parser-generated-check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peg_generator/generate.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_custom_target(ast-generated-check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/asdl_generator/generate.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
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

    add_executable(test_parser Tests/cpp/test_parser.cpp Tests/cpp/test_tokenizer.cpp)
    target_link_libraries(test_parser PRIVATE cxxpython_parser GTest::gtest_main Threads::Threads)
    target_compile_options(test_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)
    gtest_discover_tests(test_parser PROPERTIES TIMEOUT ${CXXPYTHON_TEST_TIMEOUT})

    add_library(test_extension_static OBJECT Tests/extensions/example.c)
    target_include_directories(test_extension_static PRIVATE Include)
    target_compile_options(test_extension_static PRIVATE -Wall -Wextra -Wpedantic -Werror)
    add_library(test_extension_dynamic MODULE Tests/extensions/example.c)
    target_include_directories(test_extension_dynamic PRIVATE Include)
    target_compile_options(test_extension_dynamic PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(test_extension_dynamic PROPERTIES
        PREFIX "" OUTPUT_NAME "_example.cxxpy" SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/extension-fixtures")

    add_executable(test_runtime
        Tests/cpp/test_memory.cpp
        Tests/cpp/test_gc_epochs.cpp
        Tests/cpp/test_gc_inspection.cpp
        Tests/cpp/test_parallel_vm.cpp
        Tests/cpp/test_types.cpp
        Tests/cpp/test_call.cpp
        Tests/cpp/test_attributes.cpp
        Tests/cpp/test_construction.cpp
        Tests/cpp/test_constructors.cpp
        Tests/cpp/test_bytes.cpp
        Tests/cpp/test_codecs.cpp
        Tests/cpp/test_codecs_parallel.cpp
        Tests/cpp/test_buffers_parallel.cpp
        Tests/cpp/test_fileio_parallel.cpp
        Tests/cpp/test_native_threads.cpp
        Tests/cpp/test_abc_parallel.cpp
        Tests/cpp/test_super.cpp
        Tests/cpp/test_set.cpp
        Tests/cpp/test_set_operations.cpp
        Tests/cpp/test_set_concurrency.cpp
        Tests/cpp/test_runpy.cpp
        Tests/cpp/test_sys.cpp
        Tests/cpp/test_interactive.cpp
        Tests/cpp/test_generators.cpp
        Tests/cpp/test_generators_source.cpp
        Tests/cpp/test_generators_parallel.cpp
        Tests/cpp/test_finalization.cpp
        Tests/cpp/test_weakrefs.cpp
        Tests/cpp/test_error_hooks.cpp
        Tests/cpp/test_stringformat.cpp
        Tests/cpp/test_unicode_methods.cpp
        Tests/cpp/test_builtin_functions.cpp
        Tests/cpp/test_builtin_exec.cpp
        Tests/cpp/test_stdio.cpp
        Tests/cpp/test_modules.cpp
        Tests/cpp/test_import.cpp
        Tests/cpp/test_import_protocol.cpp
        Tests/cpp/test_extension.cpp
        Tests/cpp/test_dict.cpp
        Tests/cpp/test_list.cpp
        Tests/cpp/test_import_files.cpp
        Tests/cpp/test_source_import.cpp
        Tests/cpp/test_tuple_constructor.cpp
        Tests/cpp/test_layout.cpp
        Tests/cpp/test_cell.cpp
        Tests/cpp/test_readiness.cpp
        Tests/cpp/test_protocol.cpp
        Tests/cpp/test_number.cpp
        Tests/cpp/test_representation.cpp
        Tests/cpp/test_execution.cpp
        Tests/cpp/test_sre.cpp
        Tests/cpp/test_scheduling.cpp
        Tests/cpp/test_traceback.cpp
        Tests/cpp/test_unwind.cpp
        Tests/cpp/test_with.cpp
        Tests/cpp/test_compiler.cpp
        Tests/cpp/test_iteration.cpp)
    target_link_libraries(test_runtime PRIVATE cxxpython_core GTest::gtest_main test_extension_static)
    add_dependencies(test_runtime test_extension_dynamic)
    target_compile_definitions(test_runtime PRIVATE
        TEST_EXTENSION_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/extension-fixtures")
    target_compile_options(test_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
    gtest_discover_tests(test_runtime PROPERTIES TIMEOUT ${CXXPYTHON_TEST_TIMEOUT})

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(CXXPYTHON_REFERENCE_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
        "CPython executable used for source differential tests")
    add_test(NAME Source.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_source.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Source.Differential PROPERTIES TIMEOUT 60)
    add_test(NAME Main.Entry COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_main.py $<TARGET_FILE:cxxpython>)
    set_tests_properties(Main.Entry PROPERTIES TIMEOUT 90)
    add_test(NAME VM.Dis COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_dis.py --binary $<TARGET_FILE:cxxpython>)
    set_tests_properties(VM.Dis PROPERTIES TIMEOUT 30)
    add_test(NAME GC.Inspection COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_cases.py
        --binary $<TARGET_FILE:cxxpython> --cases ${CMAKE_CURRENT_SOURCE_DIR}/Tests/vm_cases
        --case gc_vm)
    set_tests_properties(GC.Inspection PROPERTIES TIMEOUT 30)
    if(TARGET PkgConfig::Ncurses)
        add_test(NAME Curses.Terminal COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_curses.py
            --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
        set_tests_properties(Curses.Terminal PROPERTIES TIMEOUT 60)
    endif()
    if(TARGET PkgConfig::Readline)
        add_test(NAME Readline.Terminal COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_readline.py --binary $<TARGET_FILE:cxxpython>)
        add_test(NAME Main.Terminal COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_repl.py --binary $<TARGET_FILE:cxxpython>)
        set_tests_properties(Readline.Terminal Main.Terminal PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME Generators.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_generators.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Generators.Differential PROPERTIES TIMEOUT 90)
    add_test(NAME Scripts.Cases COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_cases.py --binary $<TARGET_FILE:cxxpython>)
    add_test(NAME Scripts.Reference COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_cases.py --binary ${CXXPYTHON_REFERENCE_PYTHON})
    add_test(NAME Scripts.NoGil COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_cases.py
        --binary $<TARGET_FILE:cxxpython> --cases ${CMAKE_CURRENT_SOURCE_DIR}/Tests/nogil_cases)
    set_tests_properties(Scripts.Cases Scripts.Reference PROPERTIES TIMEOUT 120)
    set_tests_properties(Scripts.NoGil PROPERTIES TIMEOUT 60)
    add_test(NAME Stdlib.Doctests COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_stdlib.py --binary $<TARGET_FILE:cxxpython>)
    set_tests_properties(Stdlib.Doctests PROPERTIES TIMEOUT 45)
    add_test(NAME Syntax.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_syntax.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Syntax.Differential PROPERTIES TIMEOUT 90)
    add_test(NAME Grammar.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_grammar.py
        --binary $<TARGET_FILE:parser_probe> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Grammar.Differential PROPERTIES TIMEOUT 90)
    add_test(NAME Grammar.Generated COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peg_generator/generate.py --check)
    add_test(NAME Ast.Generated COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tools/asdl_generator/generate.py --check)
    add_test(NAME Constructors.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_constructors.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Constructors.Differential PROPERTIES TIMEOUT 60)
    add_test(NAME Unittest.Cli COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_unittest_cli.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON}
        --timeout ${CXXPYTHON_TEST_TIMEOUT})
    add_test(NAME Signal.Delivery COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_signal_delivery.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    math(EXPR UNITTEST_CLI_TIMEOUT "4 * (${CXXPYTHON_TEST_TIMEOUT} + 30) + 10")
    set_tests_properties(Unittest.Cli PROPERTIES TIMEOUT ${UNITTEST_CLI_TIMEOUT})
    set_tests_properties(Signal.Delivery PROPERTIES TIMEOUT 90)
    add_test(NAME Dict.Differential COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python/test_dict.py
        --binary $<TARGET_FILE:cxxpython> --reference ${CXXPYTHON_REFERENCE_PYTHON})
    set_tests_properties(Dict.Differential PROPERTIES TIMEOUT 60)
    add_test(NAME Benchmarks.Report COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s ${CMAKE_CURRENT_SOURCE_DIR}/Tests/python -p test_benchmarks.py)
    set_tests_properties(Benchmarks.Report PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME CodingStyle.ClangFormat
        COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror --style=file ${FORMAT_SOURCES})
    set_tests_properties(CodingStyle.ClangFormat PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 30)
endif()
