#include <gtest/gtest.h>

#include <vector>

#include "back.hpp"

namespace {

Automaton make_back_history_target() {
    // q0/B: 0 -> q1, 1 -> q0
    // q1/A: 0 -> q0, 1 -> q0
    return Automaton(2,
                     2,
                     std::vector<std::vector<Automaton::State>>{{1, 0}, {0, 0}},
                     std::vector<Automaton::Output>{"B", "A"},
                     0);
}

} // namespace

TEST(BackProtocolTest, EmptyHistoryImpliesInitialConfiguration) {
    const Automaton target = make_back_history_target();
    BackTeacher teacher(target, 0);
    BackLearner learner(target.input_count(), 3);
    BackProtocol protocol(teacher, learner);

    std::size_t observable_history_depth = 0;
    std::size_t consecutive_backtracks = 0;
    bool emptied_after_successive_backtracks = false;

    for (int step = 0; step < 200 && !learner.stopCondition(); ++step) {
        const LearningStatistics before = protocol.statistics();
        protocol.step();
        const LearningStatistics after = protocol.statistics();

        const bool queried = after.queries == before.queries + 1;
        const bool backtracked = after.backs == before.backs + 1;
        ASSERT_FALSE(queried && backtracked);

        if (queried || backtracked) {
            if (observable_history_depth > 0) {
                --observable_history_depth;
            }
        } else {
            ++observable_history_depth;
        }

        consecutive_backtracks = backtracked ? consecutive_backtracks + 1 : 0;
        if (observable_history_depth == 0 && consecutive_backtracks >= 2) {
            emptied_after_successive_backtracks = true;
            break;
        }
    }

    ASSERT_TRUE(emptied_after_successive_backtracks);
    EXPECT_GE(protocol.statistics().backs, 2u);
    EXPECT_EQ(teacher.current_state(), target.initial_state());
    EXPECT_EQ(learner.current_state(), learner.automaton().initial_state());
    EXPECT_TRUE(learner.current_path().empty());
}

TEST(BackProtocolTest, UnfinishedLearningPreventsEmptyStackClosedBacktrack) {
    const Automaton target = make_back_history_target();
    BackTeacher teacher(target, 0);
    BackLearner learner(target.input_count(), 0);
    BackProtocol protocol(teacher, learner);

    // Learn and finalize the initial state with empty history.
    protocol.step();
    ASSERT_EQ(protocol.statistics().queries, 1u);
    ASSERT_EQ(protocol.statistics().backs, 0u);
    ASSERT_TRUE(learner.automaton().is_complete(learner.automaton().initial_state()));

    // Move forward once, learn the reached state, and backtrack to the sole
    // saved configuration. Observable history is empty again at this point.
    protocol.step();
    ASSERT_EQ(protocol.statistics().queries, 1u);
    ASSERT_EQ(protocol.statistics().backs, 0u);
    protocol.step();
    ASSERT_EQ(protocol.statistics().queries, 2u);
    ASSERT_EQ(protocol.statistics().backs, 0u);

    ASSERT_FALSE(learner.stopCondition());
    ASSERT_GT(learner.incomplete_state_count(), 0u);
    ASSERT_EQ(teacher.current_state(), target.initial_state());
    ASSERT_EQ(learner.current_state(), learner.automaton().initial_state());
    ASSERT_TRUE(learner.current_path().empty());
    ASSERT_FALSE(learner.automaton().is_closed(learner.current_state()));

    const auto next_prediction = learner.observe();
    EXPECT_FALSE(next_prediction.starts_with("!"));

    const LearningStatistics before = protocol.statistics();
    protocol.step();
    const LearningStatistics after = protocol.statistics();
    EXPECT_EQ(after.backs, before.backs);
    EXPECT_EQ(after.trials, before.trials + 1);
}
