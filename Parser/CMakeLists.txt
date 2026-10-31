cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cxxpython_parser LANGUAGES CXX)
endif()

add_library(cxxpython_parser STATIC generated_parser.cpp tokenizer.cpp string_parser.cpp unicode.cpp unicode_names.cpp)
target_compile_features(cxxpython_parser PUBLIC cxx_std_20)
target_include_directories(cxxpython_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../Include)
target_compile_options(cxxpython_parser PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(parser_probe ../Tools/parser/parse.cpp)
target_link_libraries(parser_probe PRIVATE cxxpython_parser)
target_compile_options(parser_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
