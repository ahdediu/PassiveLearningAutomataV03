#include <gtest/gtest.h>
#include "resetX.hpp"
#include "back.hpp"

using StateId = LearnerAutomaton::StateId;

TEST(ResetXExtraInfoTest, PreservationAcrossMerge) {
    // Alphabet size 2, signature depth 1.
    LearnerAutomaton model(2, 1);
    model.create_initial_state(); // S0

    // S0: root='A', path(0)='B', path(1)='B'
    (void)model.state(0).incompleteSignature.read_value_at_path({});
    model.state(0).incompleteSignature.set_value_at_last_read_node("A");
    (void)model.state(0).incompleteSignature.read_value_at_path({0});
    model.state(0).incompleteSignature.set_value_at_last_read_node("B");
    (void)model.state(0).incompleteSignature.read_value_at_path({1});
    model.state(0).incompleteSignature.set_value_at_last_read_node("B");

    model.finalize_state(0);
    StateId s1 = model.transition(0, 0);
    StateId s2 = model.transition(0, 1);

    // S1 and S2 are now incomplete.
    // Let's learn S1 normally (depth 1).
    // S1: root='B', path(0)='C', path(1)='C'
    (void)model.state(s1).incompleteSignature.read_value_at_path({});
    model.state(s1).incompleteSignature.set_value_at_last_read_node("B");
    (void)model.state(s1).incompleteSignature.read_value_at_path({0});
    model.state(s1).incompleteSignature.set_value_at_last_read_node("C");
    (void)model.state(s1).incompleteSignature.read_value_at_path({1});
    model.state(s1).incompleteSignature.set_value_at_last_read_node("C");

    model.finalize_state(s1); // S1 complete. its children s1_0 and s1_1 created.
    StateId s1_0 = model.transition(s1, 0);

    // Now for S2, we learn it but also gather extra info.
    // S2: root='B', path(0)='C', path(1)='C'
    // Extra info: path(0, 0) from S2 is 'D'.
    (void)model.state(s2).incompleteSignature.read_value_at_path({});
    model.state(s2).incompleteSignature.set_value_at_last_read_node("B");
    (void)model.state(s2).incompleteSignature.read_value_at_path({0});
    model.state(s2).incompleteSignature.set_value_at_last_read_node("C");
    (void)model.state(s2).incompleteSignature.read_value_at_path({1});
    model.state(s2).incompleteSignature.set_value_at_last_read_node("B"); // Wait, let's make it match S1 to trigger merge.
    
    // Actually, S1 has children with 'C', 'C' outputs.
    // Let's re-align S2 to match S1.
    (void)model.state(s2).incompleteSignature.read_value_at_path({1});
    model.state(s2).incompleteSignature.set_value_at_last_read_node("C");

    // Add extra info to S2 for path (0, 0)
    (void)model.state(s2).incompleteSignature.read_value_at_path({0, 0});
    model.state(s2).incompleteSignature.set_value_at_last_read_node("D");

    // S2 should now merge into S1.
    StateId canonical = model.finalize_state(s2);
    ASSERT_EQ(canonical, s1);
    ASSERT_EQ(model.state(s2).status, LearnerAutomaton::StateStatus::Merged);

    // Verify that the extra info 'D' for path (0) of S2's child 0 was pushed to S1's child 0.
    EXPECT_EQ(model.state(s1_0).incompleteSignature.peek_value_at_path({0}), "D");
}

TEST(BackExtraInfoTest, RecursivePromotionBenefits) {
    // Test if recursive promotion in Back/ResetX immediately finalizes states
    // when they inherit enough info.
    
    // d=1.
    // S0 --0--> S1 --0--> S2
    // If we gather info for path (0, 0) and (0, 0, 0) while at S0.
    // Path (0) is S1.
    // Path (0, 0) is S1's path (0), which is S2.
    // Path (0, 0, 0) is S1's path (0, 0), which is S2's path (0).
    
    LearnerAutomaton model(1, 1);
    model.create_initial_state(); // S0
    
    // S0: root='A', path(0)='B', path(0, 0)='C', path(0, 0, 0)='D'
    (void)model.state(0).incompleteSignature.read_value_at_path({});
    model.state(0).incompleteSignature.set_value_at_last_read_node("A");
    (void)model.state(0).incompleteSignature.read_value_at_path({0});
    model.state(0).incompleteSignature.set_value_at_last_read_node("B");
    
    // Extra info beyond d=1
    (void)model.state(0).incompleteSignature.read_value_at_path({0, 0});
    model.state(0).incompleteSignature.set_value_at_last_read_node("C");
    (void)model.state(0).incompleteSignature.read_value_at_path({0, 0, 0});
    model.state(0).incompleteSignature.set_value_at_last_read_node("D");
    
    // Check if S0 is ready.
    ASSERT_TRUE(model.is_ready_to_finalize(0));
    
    // Finalize S0.
    // ResetX/Back use recursive promotion. Let's simulate it.
    std::queue<StateId> to_check;
    to_check.push(0);
    int promotions = 0;
    while(!to_check.empty()) {
        StateId id = to_check.front();
        to_check.pop();
        if (model.is_ready_to_finalize(id)) {
            StateId promoted = model.finalize_state(id);
            promotions++;
            if (promoted == id) {
                for (StateId child : model.children_of(promoted)) {
                    to_check.push(child);
                }
            }
        }
    }
    
    // S0 should be finalized.
    // S1 should be created and should have inherited (0)='C' and (0, 0)='D'.
    // Wait, (0, 0) at S0 becomes (0) at S1. (0, 0, 0) at S0 becomes (0, 0) at S1.
    // d=1, so S1 needs root and (0).
    // Root of S1 is 'B'. Path(0) of S1 is 'C'.
    // So S1 should also be ready to finalize!
    // S2 will be created and inherit (0)='D'.
    // S2 needs root and (0). Root of S2 is 'C'. Path(0) of S2 is 'D'.
    // So S2 should also be ready to finalize!
    
    EXPECT_GE(promotions, 3);
    EXPECT_EQ(model.state(0).status, LearnerAutomaton::StateStatus::Complete);
    
    StateId s1 = model.transition(0, 0);
    EXPECT_EQ(model.state(s1).status, LearnerAutomaton::StateStatus::Complete);
    
    StateId s2 = model.transition(s1, 0);
    EXPECT_EQ(model.state(s2).status, LearnerAutomaton::StateStatus::Complete);
    
    StateId s3 = model.transition(s2, 0);
    EXPECT_EQ(model.state(s3).status, LearnerAutomaton::StateStatus::Incomplete);
    EXPECT_EQ(model.state(s3).incompleteSignature.peek_value_at_path({}), "D");
}
