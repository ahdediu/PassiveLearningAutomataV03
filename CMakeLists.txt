cmake_minimum_required(VERSION 4.1)
project(PassiveLearningAutomataV03)

set(CMAKE_CXX_STANDARD 20)
include(FetchContent)

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)

FetchContent_MakeAvailable(googletest)

enable_testing()
add_library(autov03 INTERFACE
        src/examples/automata_examples.hpp)
target_include_directories(autov03 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(test_automata
        tests/test_automaton.cpp
        #tests/test_signature.cpp
        #tests/test_teacher.cpp
        #tests/test_reset_learner.cpp
        #tests/test_sessions.cpp
)

target_link_libraries(test_automata
        PRIVATE
        autov03
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_automata)
