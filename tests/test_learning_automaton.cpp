#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "core/learnerAutomaton.hpp"

namespace {

using LearnerAutomaton = ::LearnerAutomaton;
using StateId = LearnerAutomaton::StateId;
using Output = LearnerAutomaton::Output;
using Path = LearnerAutomaton::Path;

void set_node_value(SignatureTree& tree, const Path& path, const Output& value) {
    const auto current = tree.read_value_at_path(path);
    EXPECT_EQ(current, "?");
    tree.set_value_at_last_read_node(value);
}

void make_complete_depth_0_signature(SignatureTree& tree, const Output& root_output) {
    set_node_value(tree, {}, root_output);
}

void make_complete_depth_1_signature(SignatureTree& tree,
                                     const Output& root_output,
                                     const std::vector<Output>& child_outputs) {
    ASSERT_EQ(child_outputs.size(), 2u);
    set_node_value(tree, {}, root_output);
    set_node_value(tree, Path{0}, child_outputs[0]);
    set_node_value(tree, Path{1}, child_outputs[1]);
}

} // namespace

TEST(LearnerAutomatonTest, RejectsInvalidConstructionParameters) {
    EXPECT_THROW((LearnerAutomaton(0, 0)), std::runtime_error);
    EXPECT_THROW((LearnerAutomaton(2, -1)), std::runtime_error);
}

TEST(LearnerAutomatonTest, CreatesInitialIncompleteState) {
    LearnerAutomaton la(2, 1);

    const StateId id = la.create_initial_state();

    EXPECT_EQ(id, 0u);
    EXPECT_TRUE(la.has_initial_state());
    EXPECT_EQ(la.initial_state(), 0u);
    EXPECT_EQ(la.state_count(), 1u);

    const auto& node = la.state(id);
    EXPECT_EQ(node.id, 0u);
    EXPECT_EQ(node.status, LearnerAutomaton::StateStatus::Incomplete);
    EXPECT_EQ(node.output, "?");
    EXPECT_EQ(node.completeSignature, "");
    EXPECT_EQ(node.mergedInto, LearnerAutomaton::invalidStateId);
    EXPECT_EQ(node.transitions.size(), 2u);
    EXPECT_EQ(node.transitions[0], LearnerAutomaton::invalidStateId);
    EXPECT_EQ(node.transitions[1], LearnerAutomaton::invalidStateId);
}

TEST(LearnerAutomatonTest, FinalizesDepth0StateWithoutMerge) {
    LearnerAutomaton la(2, 0);
    const StateId id = la.create_initial_state();

    auto& node = la.state(id);
    auto& sig = node.incompleteSignature;

    EXPECT_EQ(sig.read_value_at_path({}), "?");
    sig.set_value_at_last_read_node("+");
    EXPECT_TRUE(sig.is_complete());

    const StateId returned = la.finalize_state(id);

    EXPECT_EQ(returned, id);
    EXPECT_EQ(la.state_count(), 3u);

    const auto& final_node = la.state(id);
    EXPECT_EQ(final_node.status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(final_node.output, "+");
    EXPECT_FALSE(final_node.completeSignature.empty());
    EXPECT_TRUE(la.contains_complete_signature(final_node.completeSignature));
    EXPECT_EQ(la.state_id_of_complete_signature(final_node.completeSignature), id);

    const StateId left = la.transition(id, 0);
    const StateId right = la.transition(id, 1);
    ASSERT_NE(left, right);
    EXPECT_TRUE(la.is_incomplete(left));
    EXPECT_TRUE(la.is_incomplete(right));
    EXPECT_FALSE(la.is_ready_to_finalize(left));
    EXPECT_FALSE(la.is_ready_to_finalize(right));
    EXPECT_EQ(la.state(left).incompleteSignature.peek_value_at_path({}), "?");
    EXPECT_EQ(la.state(right).incompleteSignature.peek_value_at_path({}), "?");
}

TEST(LearnerAutomatonTest, FinalizesChildrenAndKeepsDistinctCompleteStatesWhenSignaturesDiffer) {
    LearnerAutomaton la(2, 1);
    const StateId root = la.create_initial_state();

    auto& root_node = la.state(root);
    make_complete_depth_1_signature(root_node.incompleteSignature, "R", {"A", "B"});

    const StateId canonical_root = la.finalize_state(root);
    EXPECT_EQ(canonical_root, root);
    EXPECT_EQ(la.state(root).status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(la.state_count(), 3u);

    const StateId left = la.state(root).transitions[0];
    const StateId right = la.state(root).transitions[1];

    ASSERT_NE(left, LearnerAutomaton::invalidStateId);
    ASSERT_NE(right, LearnerAutomaton::invalidStateId);
    ASSERT_NE(left, right);

    auto& left_signature = la.state(left).incompleteSignature;
    auto& right_signature = la.state(right).incompleteSignature;

    EXPECT_EQ(left_signature.peek_value_at_path({}), "A");
    EXPECT_EQ(right_signature.peek_value_at_path({}), "B");

    set_node_value(left_signature, {0}, "X");
    set_node_value(left_signature, {1}, "Y");
    set_node_value(right_signature, {0}, "X");
    set_node_value(right_signature, {1}, "Z");

    const StateId left_result = la.finalize_state(left);
    const StateId right_result = la.finalize_state(right);

    EXPECT_EQ(left_result, left);
    EXPECT_EQ(right_result, right);
    EXPECT_EQ(la.state(left).status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(la.state(right).status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_NE(la.state(left).completeSignature, la.state(right).completeSignature);
}

TEST(LearnerAutomatonTest, MergesEquivalentStatesAndRedirectsParentTransition) {
    LearnerAutomaton la(2, 1);
    const StateId root = la.create_initial_state();

    auto& root_node = la.state(root);
    make_complete_depth_1_signature(root_node.incompleteSignature, "R", {"C", "C"});

    const StateId canonical_root = la.finalize_state(root);
    EXPECT_EQ(canonical_root, root);
    EXPECT_EQ(la.state_count(), 3u);

    const StateId first_child = la.state(root).transitions[0];
    const StateId second_child = la.state(root).transitions[1];

    ASSERT_NE(first_child, LearnerAutomaton::invalidStateId);
    ASSERT_NE(second_child, LearnerAutomaton::invalidStateId);
    ASSERT_NE(first_child, second_child);

    auto& first_signature = la.state(first_child).incompleteSignature;
    auto& second_signature = la.state(second_child).incompleteSignature;

    EXPECT_EQ(first_signature.peek_value_at_path({}), "C");
    EXPECT_EQ(second_signature.peek_value_at_path({}), "C");

    set_node_value(first_signature, {0}, "X");
    set_node_value(first_signature, {1}, "X");
    set_node_value(second_signature, {0}, "X");
    set_node_value(second_signature, {1}, "X");

    const StateId canonical_first = la.finalize_state(first_child);
    EXPECT_EQ(canonical_first, first_child);
    EXPECT_EQ(la.state(first_child).status, LearnerAutomaton::StateStatus::Complete);

    const StateId merged_result = la.finalize_state(second_child);
    EXPECT_EQ(merged_result, first_child);

    const auto& merged_node = la.state(second_child);
    EXPECT_EQ(merged_node.status, LearnerAutomaton::StateStatus::Merged);
    EXPECT_EQ(merged_node.mergedInto, first_child);
    EXPECT_EQ(la.state(root).transitions[1], first_child);
}

TEST(LearnerAutomatonTest, RejectsInvalidStateAndTransitionAccess) {
    LearnerAutomaton la(2, 0);

    EXPECT_THROW(
        {
            static_cast<void>(la.initial_state());
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            static_cast<void>(la.state(0));
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            static_cast<void>(la.transition(0, 0));
        },
        std::runtime_error
    );

    la.create_initial_state();

    EXPECT_THROW(
        {
            static_cast<void>(la.state(1));
        },
        std::runtime_error
    );

    EXPECT_THROW(
        {
            static_cast<void>(la.transition(0, 2));
        },
        std::runtime_error
    );
}

TEST(LearnerAutomatonTest, RejectsFinalizingNonIncompleteState) {
    LearnerAutomaton la(2, 0);
    const StateId id = la.create_initial_state();

    auto& node = la.state(id);
    make_complete_depth_0_signature(node.incompleteSignature, "+");
    la.finalize_state(id);

    EXPECT_THROW(la.finalize_state(id), std::runtime_error);

    const StateId left = la.transition(id, 0);
    const StateId right = la.transition(id, 1);
    ASSERT_NE(left, right);
    EXPECT_TRUE(la.is_incomplete(left));
    EXPECT_TRUE(la.is_incomplete(right));
    EXPECT_FALSE(la.is_ready_to_finalize(left));
    EXPECT_FALSE(la.is_ready_to_finalize(right));
}

TEST(LearnerAutomatonTest, ContainsKnownCompleteSignature) {
    LearnerAutomaton la(2, 0);
    const StateId id = la.create_initial_state();

    auto& node = la.state(id);
    make_complete_depth_0_signature(node.incompleteSignature, "+");
    la.finalize_state(id);

    EXPECT_TRUE(la.contains_complete_signature(la.state(id).completeSignature));
    EXPECT_EQ(la.state_id_of_complete_signature(la.state(id).completeSignature), id);

    EXPECT_THROW(
        [&] {
            static_cast<void>(la.state_id_of_complete_signature("missing"));
        }(),
        std::runtime_error
    );
}

TEST(LearnerAutomatonTest, FinalizedDepth1StatePropagatesChildSignatures) {
    LearnerAutomaton la(2, 1);
    const StateId root = la.create_initial_state();

    auto& root_node = la.state(root);
    auto& sig = root_node.incompleteSignature;

    EXPECT_EQ(sig.read_value_at_path({}), "?");
    sig.set_value_at_last_read_node("R");

    EXPECT_EQ(sig.read_value_at_path({0}), "?");
    sig.set_value_at_last_read_node("A");

    EXPECT_EQ(sig.read_value_at_path({1}), "?");
    sig.set_value_at_last_read_node("B");

    EXPECT_TRUE(sig.is_complete());

    const StateId returned = la.finalize_state(root);
    EXPECT_EQ(returned, root);

    const auto& final_root = la.state(root);
    EXPECT_EQ(final_root.status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(final_root.output, "R");
    ASSERT_EQ(final_root.transitions.size(), 2u);

    const StateId left = final_root.transitions[0];
    const StateId right = final_root.transitions[1];

    ASSERT_NE(left, LearnerAutomaton::invalidStateId);
    ASSERT_NE(right, LearnerAutomaton::invalidStateId);
    ASSERT_NE(left, right);

    auto& left_node = la.state(left);
    auto& right_node = la.state(right);

    EXPECT_EQ(left_node.status, LearnerAutomaton::StateStatus::Incomplete);
    EXPECT_EQ(right_node.status, LearnerAutomaton::StateStatus::Incomplete);

    EXPECT_EQ(left_node.incompleteSignature.read_value_at_path({}), "A");
    EXPECT_EQ(right_node.incompleteSignature.read_value_at_path({}), "B");
}

TEST(LearnerAutomatonTest, FinalizingBoundedOnlySignatureNeverCreatesReadySuccessor) {
    LearnerAutomaton la(2, 2);
    const StateId root = la.create_initial_state();

    auto& signature = la.state(root).incompleteSignature;
    set_node_value(signature, {}, "R");
    set_node_value(signature, {0}, "A");
    set_node_value(signature, {1}, "B");
    set_node_value(signature, {0, 0}, "A0");
    set_node_value(signature, {0, 1}, "A1");
    set_node_value(signature, {1, 0}, "B0");
    set_node_value(signature, {1, 1}, "B1");

    ASSERT_TRUE(signature.is_complete());
    ASSERT_EQ(la.finalize_state(root), root);

    const StateId left = la.transition(root, 0);
    const StateId right = la.transition(root, 1);

    ASSERT_TRUE(la.is_incomplete(left));
    ASSERT_TRUE(la.is_incomplete(right));
    EXPECT_FALSE(la.is_ready_to_finalize(left));
    EXPECT_FALSE(la.is_ready_to_finalize(right));

    const auto& left_signature = la.state(left).incompleteSignature;
    EXPECT_EQ(left_signature.peek_value_at_path({}), "A");
    EXPECT_EQ(left_signature.peek_value_at_path({0}), "A0");
    EXPECT_EQ(left_signature.peek_value_at_path({1}), "A1");
    EXPECT_EQ(left_signature.peek_value_at_path({0, 0}), "?");
    EXPECT_EQ(left_signature.peek_value_at_path({0, 1}), "?");
    EXPECT_EQ(left_signature.peek_value_at_path({1, 0}), "?");
    EXPECT_EQ(left_signature.peek_value_at_path({1, 1}), "?");

    const auto& right_signature = la.state(right).incompleteSignature;
    EXPECT_EQ(right_signature.peek_value_at_path({}), "B");
    EXPECT_EQ(right_signature.peek_value_at_path({0}), "B0");
    EXPECT_EQ(right_signature.peek_value_at_path({1}), "B1");
    EXPECT_EQ(right_signature.peek_value_at_path({0, 0}), "?");
    EXPECT_EQ(right_signature.peek_value_at_path({0, 1}), "?");
    EXPECT_EQ(right_signature.peek_value_at_path({1, 0}), "?");
    EXPECT_EQ(right_signature.peek_value_at_path({1, 1}), "?");

    EXPECT_EQ(left_signature.missing_count(), 4u);
    EXPECT_EQ(right_signature.missing_count(), 4u);
}

TEST(LearnerAutomatonTest, LearnsThreeStateAutomatonWithOneSelfLoopAndOneAbsorbingState) {
    LearnerAutomaton la(2, 1);

    const StateId root = la.create_initial_state();
    auto& root_signature = la.state(root).incompleteSignature;
    make_complete_depth_1_signature(root_signature, "R", {"S", "T"});
    ASSERT_EQ(la.finalize_state(root), root);

    const StateId q_s = la.transition(root, 0);
    const StateId q_t = la.transition(root, 1);
    ASSERT_NE(q_s, q_t);

    auto& q_s_signature = la.state(q_s).incompleteSignature;
    EXPECT_EQ(q_s_signature.peek_value_at_path({}), "S");
    set_node_value(q_s_signature, {0}, "S");
    set_node_value(q_s_signature, {1}, "T");
    ASSERT_EQ(la.finalize_state(q_s), q_s);

    auto& q_t_signature = la.state(q_t).incompleteSignature;
    EXPECT_EQ(q_t_signature.peek_value_at_path({}), "T");
    set_node_value(q_t_signature, {0}, "T");
    set_node_value(q_t_signature, {1}, "T");
    ASSERT_EQ(la.finalize_state(q_t), q_t);

    const StateId q_s_via_s_0 = la.transition(q_s, 0);
    const StateId q_t_via_s_1 = la.transition(q_s, 1);
    const StateId q_t_via_t_0 = la.transition(q_t, 0);
    const StateId q_t_via_t_1 = la.transition(q_t, 1);

    auto complete_frontier_signature = [&](StateId id,
                                           const Output& input_0_output,
                                           const Output& input_1_output) {
        auto& signature = la.state(id).incompleteSignature;
        set_node_value(signature, {0}, input_0_output);
        set_node_value(signature, {1}, input_1_output);
    };

    complete_frontier_signature(q_s_via_s_0, "S", "T");
    EXPECT_EQ(la.finalize_state(q_s_via_s_0), q_s);

    complete_frontier_signature(q_t_via_s_1, "T", "T");
    EXPECT_EQ(la.finalize_state(q_t_via_s_1), q_t);

    complete_frontier_signature(q_t_via_t_0, "T", "T");
    EXPECT_EQ(la.finalize_state(q_t_via_t_0), q_t);

    complete_frontier_signature(q_t_via_t_1, "T", "T");
    EXPECT_EQ(la.finalize_state(q_t_via_t_1), q_t);

    EXPECT_EQ(la.state_count(), 7u);

    std::size_t complete_count = 0;
    std::size_t merged_count = 0;
    std::size_t incomplete_count = 0;
    for (StateId id = 0; id < la.state_count(); ++id) {
        if (la.is_complete(id)) {
            ++complete_count;
        } else if (la.is_merged(id)) {
            ++merged_count;
        } else if (la.is_incomplete(id)) {
            ++incomplete_count;
        }
    }

    EXPECT_EQ(complete_count, 3u);
    EXPECT_EQ(merged_count, 4u);
    EXPECT_EQ(incomplete_count, 0u);

    EXPECT_EQ(la.transition(root, 0), q_s);
    EXPECT_EQ(la.transition(root, 1), q_t);
    EXPECT_EQ(la.transition(q_s, 0), q_s);
    EXPECT_EQ(la.transition(q_s, 1), q_t);
    EXPECT_EQ(la.transition(q_t, 0), q_t);
    EXPECT_EQ(la.transition(q_t, 1), q_t);

    EXPECT_EQ(la.state(root).completeSignature, "(R(S)(T))");
    EXPECT_EQ(la.state(q_s).completeSignature, "(S(S)(T))");
    EXPECT_EQ(la.state(q_t).completeSignature, "(T(T)(T))");
}
