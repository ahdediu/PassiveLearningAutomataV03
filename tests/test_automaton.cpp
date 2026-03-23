//
// Created by Adrian Dediu on 20/03/2026.
//

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/automaton.hpp"
#include "examples/automata_examples.hpp"

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

bool same_partition_shape(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        for (std::size_t j = 0; j < a.size(); ++j) {
            const bool same_a = (a[i] == a[j]);
            const bool same_b = (b[i] == b[j]);
            if (same_a != same_b) {
                return false;
            }
        }
    }

    return true;
}

} // namespace

class AutomatonFixture : public ::testing::Test {
protected:
    using State = Automaton::State;
    using Symbol = Automaton::Symbol;
    using Output = Automaton::Output;
};

TEST_F(AutomatonFixture, ConstructsAndAccessesExamplesCorrectly) {
    {
        auto a = examples::two_state_flip();

        EXPECT_EQ(a.input_count(), 2u);
        EXPECT_EQ(a.state_count(), 2u);
        EXPECT_EQ(a.initial_state(), 0u);

        EXPECT_EQ(a.output(0), "+");
        EXPECT_EQ(a.output(1), "-");

        // next_state(symbol, state)
        EXPECT_EQ(a.next_state(0, 0), 1u);
        EXPECT_EQ(a.next_state(1, 0), 0u);
        EXPECT_EQ(a.next_state(0, 1), 0u);
        EXPECT_EQ(a.next_state(1, 1), 1u);
    }

    {
        auto a = examples::three_state_cycle();

        EXPECT_EQ(a.input_count(), 2u);
        EXPECT_EQ(a.state_count(), 3u);
        EXPECT_EQ(a.initial_state(), 0u);

        std::vector<Symbol> word = {0, 1};

        EXPECT_EQ(a.next_state(0, word), 0u);
        EXPECT_EQ(a.output_at(0, word), "A");
    }
}

TEST_F(AutomatonFixture, ParsesSymbolicAndNumericSpecifications) {
    {
        auto a = examples::parser_symbolic_example();

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

    {
        auto a = examples::parser_numeric_example();

        EXPECT_EQ(a.input_count(), 2u);
        EXPECT_EQ(a.state_count(), 3u);
        EXPECT_EQ(a.initial_state(), 0u);

        EXPECT_EQ(a.output(0), "5");
        EXPECT_EQ(a.output(1), "7");
        EXPECT_EQ(a.output(2), "9");

        EXPECT_EQ(a.next_state(0, 0), 1u);
        EXPECT_EQ(a.next_state(1, 0), 2u);
        EXPECT_EQ(a.next_state(0, 1), 2u);
        EXPECT_EQ(a.next_state(1, 1), 1u);
    }
}

TEST_F(AutomatonFixture, RejectsInvalidAutomataSpecificationsAndAccess) {
    {
        std::vector<std::vector<State>> transitions;
        std::vector<Output> outputs = {"+"};

        EXPECT_THROW(
            Automaton(0, 1, transitions, outputs, 0),
            std::runtime_error
        );
    }

    {
        std::vector<std::vector<State>> transitions = {
            {1},
            {0, 1}
        };
        std::vector<Output> outputs = {"+", "-"};

        EXPECT_THROW(
            Automaton(2, 2, transitions, outputs, 0),
            std::runtime_error
        );
    }

    {
        std::vector<std::vector<State>> transitions = {
            {1, 2},
            {0, 1}
        };
        std::vector<Output> outputs = {"+", "-"};

        EXPECT_THROW(
            Automaton(2, 2, transitions, outputs, 0),
            std::runtime_error
        );
    }

    {
        std::vector<std::vector<State>> transitions = {
            {1, 0},
            {0, 1}
        };
        std::vector<Output> outputs = {"+"};

        EXPECT_THROW(
            Automaton(2, 2, transitions, outputs, 0),
            std::runtime_error
        );
    }

    {
        EXPECT_THROW(
            Automaton::from_string("2 5 3 3 0 2 0 4 0 0 1 2 +-+-+"),
            std::runtime_error
        );

        EXPECT_THROW(
            Automaton::from_string("2 3 1 2 0 2:5 7 9"),
            std::runtime_error
        );
    }

    {
        auto a = examples::two_state_flip();

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
}

TEST_F(AutomatonFixture, ReadsAutomataFilesRobustly) {
    {
        const fs::path path = make_test_file(
            "test_single_automaton.txt",
            examples::specs::parser_numeric + "\n"
        );

        const auto automata = Automaton::read_all_from_file(path.string());

        ASSERT_EQ(automata.size(), 1u);
        EXPECT_EQ(automata[0].state_count(), 3u);
        EXPECT_EQ(automata[0].input_count(), 2u);
        EXPECT_EQ(automata[0].output(0), "5");
        EXPECT_EQ(automata[0].output(1), "7");
        EXPECT_EQ(automata[0].output(2), "9");
    }

    {
        const fs::path path = make_test_file(
            "test_multiple_automata.txt",
            examples::specs::parser_numeric + "\n" +
            "2 2 1 0 0 1:+-\n"
        );

        const auto automata = Automaton::read_all_from_file(path.string());

        ASSERT_EQ(automata.size(), 2u);
        EXPECT_EQ(automata[0].state_count(), 3u);
        EXPECT_EQ(automata[1].state_count(), 2u);
        EXPECT_EQ(automata[1].output(0), "+");
        EXPECT_EQ(automata[1].output(1), "-");
    }

    {
        const fs::path path = make_test_file(
            "test_blank_and_comments.txt",
            "\n"
            "   \n"
            "\t\t\n"
            "# direct comment\n"
            "   # indented comment\n"
            "2 2 1 0 0 1:+-\n"
        );

        const auto automata = Automaton::read_all_from_file(path.string());

        ASSERT_EQ(automata.size(), 1u);
        EXPECT_EQ(automata[0].state_count(), 2u);
        EXPECT_EQ(automata[0].input_count(), 2u);
        EXPECT_EQ(automata[0].output(0), "+");
        EXPECT_EQ(automata[0].output(1), "-");
    }
}

TEST_F(AutomatonFixture, RejectsMissingInputFile) {
    EXPECT_THROW(
        Automaton::read_all_from_file("test_tmp/definitely_missing_file_12345.txt"),
        std::runtime_error
    );
}

TEST_F(AutomatonFixture, ComputesReachabilityCorrectly) {
    {
        auto a = examples::three_state_cycle();

        auto reachable = a.reachable_states();

        ASSERT_EQ(reachable.size(), 3u);
        EXPECT_TRUE(reachable.contains(0));
        EXPECT_TRUE(reachable.contains(1));
        EXPECT_TRUE(reachable.contains(2));
    }

    {
        auto a = examples::unreachable_example();

        auto reachable = a.reachable_states();

        ASSERT_EQ(reachable.size(), 2u);
        EXPECT_TRUE(reachable.contains(0));
        EXPECT_TRUE(reachable.contains(1));
        EXPECT_FALSE(reachable.contains(2));
        EXPECT_FALSE(reachable.contains(3));
    }

    {
        std::vector<std::vector<State>> transitions = {
            {2, 2, 2},
            {2, 2, 2}
        };
        std::vector<Output> outputs = {"X", "Y", "Z"};

        Automaton a(2, 3, transitions, outputs, 1);

        auto reachable = a.reachable_states();
        EXPECT_TRUE(reachable.contains(1));
    }
}

TEST_F(AutomatonFixture, RefinesPartitionsCorrectly) {
    {
        auto a = examples::degree_one_example();

        auto p0 = a.partition_by_output();
        auto p1 = a.refine_partition(p0);

        EXPECT_EQ(p0[0], p0[1]);
        EXPECT_NE(p1[0], p1[1]);

        std::vector<int> expected = {0, 1, 2, 3};
        EXPECT_TRUE(same_partition_shape(p1, expected));
    }

    {
        auto a = examples::unreachable_example();

        auto p0 = a.partition_by_output();
        auto p1 = a.refine_partition(p0);

        EXPECT_EQ(p0[2], -1);
        EXPECT_EQ(p0[3], -1);
        EXPECT_EQ(p1[2], -1);
        EXPECT_EQ(p1[3], -1);
    }

    {
        std::vector<std::vector<State>> transitions = {
            {1, 2, 0, 3},
            {2, 0, 1, 3}
        };
        std::vector<Output> outputs = {"A", "B", "A", "C"};

        Automaton a(2, 4, transitions, outputs, 0);

        auto p = a.partition_by_output();

        EXPECT_EQ(p[0], p[2]);
        EXPECT_NE(p[0], p[1]);
        EXPECT_NE(p[0], p[3]);
        EXPECT_NE(p[1], p[3]);
    }
}

TEST_F(AutomatonFixture, ComputesDistinguishabilityDegreeByPartition) {
    {
        std::vector<std::vector<State>> transitions = {
            {0, 1},
            {0, 1}
        };
        std::vector<Output> outputs = {"A", "B"};

        Automaton a(2, 2, transitions, outputs, 0);
        EXPECT_EQ(a.distinguishability_degree_by_partition(), 0);
    }

    {
        auto a = examples::degree_one_example();
        EXPECT_EQ(a.distinguishability_degree_by_partition(), 1);
    }

    {
        auto a = examples::degree_two_example();
        EXPECT_EQ(a.distinguishability_degree_by_partition(), 2);
    }
}