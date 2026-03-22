//
// Created by Adrian Dediu on 20/03/2026.
//
#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>
#include <filesystem>


#include "core/automaton.hpp"
namespace fs = std::filesystem;

namespace {
    fs::path make_test_file(const std::string& filename, const std::string& content) {
        const fs::path dir = "test_tmp";
        fs::create_directories(dir);

        const fs::path path = dir / filename;
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("Cannot create test file: " + path.string());
        }
        out << content;
        out.close();
        return path;
    }
}


TEST(AutomatonConstructionTest, ConstructsValidAutomaton) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 0}, // symbol 0
        {0, 1}  // symbol 1
    };
    std::vector<Automaton::Output> outputs = {"+", "-"};

    Automaton a(2, 2, transitions, outputs, 0);

    EXPECT_EQ(a.input_count(), 2u);
    EXPECT_EQ(a.state_count(), 2u);
    EXPECT_EQ(a.initial_state(), 0u);
}

TEST(AutomatonAccessTest, ReturnsDirectTransitionAndOutput) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 0}, // symbol 0
        {0, 1}  // symbol 1
    };
    std::vector<Automaton::Output> outputs = {"+", "-"};

    Automaton a(2, 2, transitions, outputs, 0);

    EXPECT_EQ(a.output(0), "+");
    EXPECT_EQ(a.output(1), "-");

    EXPECT_EQ(a.next_state(0, 0), 1u);
    EXPECT_EQ(a.next_state(1, 0), 0u);
    EXPECT_EQ(a.next_state(0, 1), 0u);
    EXPECT_EQ(a.next_state(1, 1), 1u);
}

TEST(AutomatonAccessTest, FollowsWordAndReturnsOutputAtDestination) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 2, 0}, // symbol 0
        {2, 0, 1}  // symbol 1
    };
    std::vector<Automaton::Output> outputs = {"A", "B", "C"};

    Automaton a(2, 3, transitions, outputs, 0);

    std::vector<Automaton::Symbol> word = {0, 1};

    // From state 0:
    // symbol 0 -> 1
    // symbol 1 -> 0
    EXPECT_EQ(a.next_state(0, word), 0u);
    EXPECT_EQ(a.output_at(0, word), "A");
}

TEST(AutomatonParsingTest, ParsesCompactSymbolicOutputs) {
    const std::string spec = "2 5 3 3 0 2 0 4 0 0 1 2:+-+-+";

    Automaton a = Automaton::from_string(spec);

    EXPECT_EQ(a.input_count(), 2u);
    EXPECT_EQ(a.state_count(), 5u);
    EXPECT_EQ(a.initial_state(), 0u);

    EXPECT_EQ(a.output(0), "+");
    EXPECT_EQ(a.output(1), "-");
    EXPECT_EQ(a.output(2), "+");
    EXPECT_EQ(a.output(3), "-");
    EXPECT_EQ(a.output(4), "+");

    EXPECT_EQ(a.next_state(0, 0), 3u);
    EXPECT_EQ(a.next_state(1, 0), 4u);
    EXPECT_EQ(a.next_state(0, 1), 3u);
    EXPECT_EQ(a.next_state(1, 1), 0u);
}

TEST(AutomatonParsingTest, ParsesWhitespaceSeparatedOutputs) {
    const std::string spec = "2 3 1 2 0 2 1 0:5 7 9";

    Automaton a = Automaton::from_string(spec);

    EXPECT_EQ(a.input_count(), 2u);
    EXPECT_EQ(a.state_count(), 3u);

    EXPECT_EQ(a.output(0), "5");
    EXPECT_EQ(a.output(1), "7");
    EXPECT_EQ(a.output(2), "9");

    EXPECT_EQ(a.next_state(0, 0), 1u);
    EXPECT_EQ(a.next_state(1, 0), 2u);
    EXPECT_EQ(a.next_state(0, 1), 2u);
    EXPECT_EQ(a.next_state(1, 1), 1u);
}

TEST(AutomatonValidationTest, RejectsZeroInputCount) {
    std::vector<std::vector<Automaton::State>> transitions;
    std::vector<Automaton::Output> outputs = {"+"};

    EXPECT_THROW(
        Automaton(0, 1, transitions, outputs, 0),
        std::runtime_error
    );
}

TEST(AutomatonValidationTest, RejectsWrongTransitionRowSize) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1},    // wrong size, should have 2 states
        {0, 1}
    };
    std::vector<Automaton::Output> outputs = {"+", "-"};

    EXPECT_THROW(
        Automaton(2, 2, transitions, outputs, 0),
        std::runtime_error
    );
}

TEST(AutomatonValidationTest, RejectsTransitionTargetOutOfRange) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 2}, // target 2 is out of range for 2 states
        {0, 1}
    };
    std::vector<Automaton::Output> outputs = {"+", "-"};

    EXPECT_THROW(
        Automaton(2, 2, transitions, outputs, 0),
        std::runtime_error
    );
}

TEST(AutomatonValidationTest, RejectsWrongNumberOfOutputs) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 0},
        {0, 1}
    };
    std::vector<Automaton::Output> outputs = {"+"}; // should have 2 outputs

    EXPECT_THROW(
        Automaton(2, 2, transitions, outputs, 0),
        std::runtime_error
    );
}

TEST(AutomatonBoundsTest, RejectsOutOfRangeStateAndSymbol) {
    std::vector<std::vector<Automaton::State>> transitions = {
        {1, 0},
        {0, 1}
    };
    std::vector<Automaton::Output> outputs = {"+", "-"};

    Automaton a(2, 2, transitions, outputs, 0);

    EXPECT_THROW(
     [&]() { static_cast<void>(a.output(2)); }(),
     std::runtime_error
 );

    EXPECT_THROW(
        [&]() { static_cast<void>(a.next_state(2, 0)); }(),
        std::runtime_error
    );

    EXPECT_THROW(
        [&]() { static_cast<void>(a.next_state(0, 2)); }(),
        std::runtime_error
    );
}

TEST(AutomatonParsingTest, RejectsMissingColon) {
    const std::string spec = "2 5 3 3 0 2 0 4 0 0 1 2 +-+-+";

    EXPECT_THROW(
        Automaton::from_string(spec),
        std::runtime_error
    );
}

TEST(AutomatonParsingTest, RejectsWrongTransitionCountInString) {
    const std::string spec = "2 3 1 2 0 2:5 7 9";

    EXPECT_THROW(
        Automaton::from_string(spec),
        std::runtime_error
    );
}

TEST(AutomatonFileTest, ReadsSingleAutomatonFromFile) {
    const fs::path path = make_test_file(
        "test_single_automaton.txt",
        "2 3 1 2 0 2 1 0:5 7 9\n"
    );

    const auto automata = Automaton::read_all_from_file(path.string());

    ASSERT_EQ(automata.size(), 1u);
    EXPECT_EQ(automata[0].state_count(), 3u);
    EXPECT_EQ(automata[0].input_count(), 2u);
    EXPECT_EQ(automata[0].output(0), "5");
    EXPECT_EQ(automata[0].output(1), "7");
    EXPECT_EQ(automata[0].output(2), "9");
}

TEST(AutomatonFileTest, ReadsMultipleAutomataFromFile) {
    const fs::path path = make_test_file(
        "test_multiple_automata.txt",
        "2 3 1 2 0 2 1 0:5 7 9\n"
        "2 2 1 0 0 1:+-\n"
    );

    const auto automata = Automaton::read_all_from_file(path.string());

    ASSERT_EQ(automata.size(), 2u);
    EXPECT_EQ(automata[0].state_count(), 3u);
    EXPECT_EQ(automata[1].state_count(), 2u);
    EXPECT_EQ(automata[1].output(0), "+");
    EXPECT_EQ(automata[1].output(1), "-");
}

TEST(AutomatonFileTest, SkipsBlankAndWhitespaceOnlyLines) {
    const fs::path path = make_test_file(
        "test_blank_lines.txt",
        "\n"
        "   \n"
        "\t\t\n"
        "2 2 1 0 0 1:+-\n"
    );

    const auto automata = Automaton::read_all_from_file(path.string());

    ASSERT_EQ(automata.size(), 1u);
    EXPECT_EQ(automata[0].state_count(), 2u);
    EXPECT_EQ(automata[0].input_count(), 2u);
}

TEST(AutomatonFileTest, SkipsCommentLinesIncludingIndentedOnes) {
    const fs::path path = make_test_file(
        "test_comment_lines.txt",
        "# direct comment\n"
        "   # indented comment\n"
        "2 2 1 0 0 1:+-\n"
    );

    const auto automata = Automaton::read_all_from_file(path.string());

    ASSERT_EQ(automata.size(), 1u);
    EXPECT_EQ(automata[0].state_count(), 2u);
    EXPECT_EQ(automata[0].output(0), "+");
    EXPECT_EQ(automata[0].output(1), "-");
}

TEST(AutomatonFileTest, RejectsMissingFile) {
    EXPECT_THROW(
        Automaton::read_all_from_file("test_tmp/definitely_missing_file_12345.txt"),
        std::runtime_error
    );
}