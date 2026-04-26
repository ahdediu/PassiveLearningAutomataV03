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
    EXPECT_EQ(la.state_count(), 1u);

    const auto& final_node = la.state(id);
    EXPECT_EQ(final_node.status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(final_node.output, "+");
    EXPECT_FALSE(final_node.completeSignature.empty());
    EXPECT_TRUE(la.contains_complete_signature(final_node.completeSignature));
    EXPECT_EQ(la.state_id_of_complete_signature(final_node.completeSignature), id);
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

    auto& left_node = la.state(left);
    auto& right_node = la.state(right);

    EXPECT_EQ(left_node.output, "A");
    EXPECT_EQ(right_node.output, "B");

    make_complete_depth_1_signature(left_node.incompleteSignature, "L", {"X", "Y"});
    make_complete_depth_1_signature(right_node.incompleteSignature, "R", {"X", "Z"});

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

    auto& first_node = la.state(first_child);
    auto& second_node = la.state(second_child);

    make_complete_depth_1_signature(first_node.incompleteSignature, "S", {"X", "X"});
    make_complete_depth_1_signature(second_node.incompleteSignature, "S", {"X", "X"});

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
    EXPECT_THROW(
        [&] {
            static_cast<void>(la.transition(id, 0));
        }(),
        std::runtime_error
    );
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

TEST(LearnerAutomatonTest, LearnsThreeStateAutomatonWithOneSelfLoopAndOneAbsorbingState) {
    LearnerAutomaton la(2, 1);

    const StateId root = la.create_initial_state();
    auto& root_node = la.state(root);

    auto& root_sig = root_node.incompleteSignature;
    EXPECT_EQ(root_sig.read_value_at_path({}), "?");
    root_sig.set_value_at_last_read_node("R");

    EXPECT_EQ(root_sig.read_value_at_path({0}), "?");
    root_sig.set_value_at_last_read_node("S");

    EXPECT_EQ(root_sig.read_value_at_path({1}), "?");
    root_sig.set_value_at_last_read_node("T");

    ASSERT_TRUE(root_sig.is_complete());

    const StateId returned_root = la.finalize_state(root);
    EXPECT_EQ(returned_root, root);

    const StateId s0 = la.state(root).transitions[0];
    const StateId s1 = la.state(root).transitions[1];

    ASSERT_NE(s0, LearnerAutomaton::invalidStateId);
    ASSERT_NE(s1, LearnerAutomaton::invalidStateId);
    ASSERT_NE(s0, s1);

    {
        auto& n0 = la.state(s0);
        auto& sig0 = n0.incompleteSignature;

        EXPECT_EQ(sig0.read_value_at_path({}), "?");
        sig0.set_value_at_last_read_node("1");

        EXPECT_EQ(sig0.read_value_at_path({0}), "?");
        sig0.set_value_at_last_read_node("1");

        EXPECT_EQ(sig0.read_value_at_path({1}), "?");
        sig0.set_value_at_last_read_node("1");

        ASSERT_TRUE(sig0.is_complete());
        EXPECT_EQ(la.finalize_state(s0), s0);
    }

    {
        auto& n1 = la.state(s1);
        auto& sig1 = n1.incompleteSignature;

        EXPECT_EQ(sig1.read_value_at_path({}), "?");
        sig1.set_value_at_last_read_node("2");

        EXPECT_EQ(sig1.read_value_at_path({0}), "?");
        sig1.set_value_at_last_read_node("2");

        EXPECT_EQ(sig1.read_value_at_path({1}), "?");
        sig1.set_value_at_last_read_node("2");

        ASSERT_TRUE(sig1.is_complete());
        EXPECT_EQ(la.finalize_state(s1), s1);
    }

    EXPECT_EQ(la.state_count(), 3u);
    EXPECT_EQ(la.state(root).status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(la.state(s0).status, LearnerAutomaton::StateStatus::Complete);
    EXPECT_EQ(la.state(s1).status, LearnerAutomaton::StateStatus::Complete);

    EXPECT_NE(la.state(s0).completeSignature, la.state(s1).completeSignature);
}

