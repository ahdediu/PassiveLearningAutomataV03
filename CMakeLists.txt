cmake_minimum_required(VERSION 4.1)
project(PassiveLearningAutomataV03)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)

FetchContent_MakeAvailable(googletest)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gtest PRIVATE -w)
    target_compile_options(gtest_main PRIVATE -w)
elseif (MSVC)
    target_compile_options(gtest PRIVATE /W0)
    target_compile_options(gtest_main PRIVATE /W0)
endif()

enable_testing()
# library to include the headers
add_library(auto_v03 INTERFACE)
target_include_directories(auto_v03 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(test_automata
        tests/test_automaton.cpp
)
target_link_libraries(test_automata
        PRIVATE
        auto_v03
        GTest::gtest_main
)

add_executable(test_signature
        tests/test_signature.cpp
)
target_link_libraries(test_signature
        PRIVATE
        auto_v03
        GTest::gtest_main
)

add_executable(test_learning_automaton
        tests/test_learning_automaton.cpp
)
target_link_libraries(test_learning_automaton
        PRIVATE
        auto_v03
        GTest::gtest_main
)

add_executable(test_back_protocol
        tests/test_back_protocol.cpp
)
target_link_libraries(test_back_protocol
        PRIVATE
        auto_v03
        GTest::gtest_main
)

add_executable(passive_learning_automata
        learning_main.cpp
        src/reset.hpp
)
target_link_libraries(passive_learning_automata
        PRIVATE
        auto_v03
)

add_subdirectory(tools/generateIndoorMaps)

include(GoogleTest)
gtest_discover_tests(test_automata)
gtest_discover_tests(test_signature)
gtest_discover_tests(test_learning_automaton)
gtest_discover_tests(test_back_protocol)
