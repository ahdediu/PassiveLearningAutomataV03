#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/signature.hpp"

namespace {

using Signature = SignatureTree;
using Symbol = Signature::Symbol;
using Path = Signature::Path;

Path make_path(std::initializer_list<Symbol> symbols) {
    return {symbols};
}

} // namespace

class SignatureFixture : public ::testing::Test {
protected:
    // ... existing code ...
};

TEST_F(SignatureFixture, ConstructsEmptyTreeWithExpectedProperties) {
    Signature s(2, 2);

    EXPECT_EQ(s.depth(), 2);
    EXPECT_EQ(s.alphabet_size(), 2u);
    EXPECT_TRUE(s.has_root());
    EXPECT_FALSE(s.is_complete());
    EXPECT_GT(s.missing_count(), 0u);
}

TEST_F(SignatureFixture, RejectsInvalidConstructionArguments) {
    EXPECT_THROW(Signature(-1, 2), std::runtime_error);
    EXPECT_THROW(Signature(2, 0), std::runtime_error);
}

TEST_F(SignatureFixture, ReadsUnknownValuesAsQuestionMark) {
    Signature s(1, 2);

    const auto root_value = s.read_value_at_path(make_path({}));
    const auto child0_value = s.read_value_at_path(make_path({0}));
    const auto child1_value = s.read_value_at_path(make_path({1}));

    EXPECT_EQ(root_value, "?");
    EXPECT_EQ(child0_value, "?");
    EXPECT_EQ(child1_value, "?");
}

TEST_F(SignatureFixture, CanSetValuesAtLastReadNode) {
    Signature s(1, 2);

    const auto root_before = s.read_value_at_path(make_path({}));
    EXPECT_EQ(root_before, "?");
    s.set_value_at_last_read_node("R");
    const auto root_after = s.read_value_at_path(make_path({}));
    EXPECT_EQ(root_after, "R");

    const auto child0_before = s.read_value_at_path(make_path({0}));
    EXPECT_EQ(child0_before, "?");
    s.set_value_at_last_read_node("A");
    const auto child0_after = s.read_value_at_path(make_path({0}));
    EXPECT_EQ(child0_after, "A");

    const auto child1_before = s.read_value_at_path(make_path({1}));
    EXPECT_EQ(child1_before, "?");
    s.set_value_at_last_read_node("B");
    const auto child1_after = s.read_value_at_path(make_path({1}));
    EXPECT_EQ(child1_after, "B");
}

TEST_F(SignatureFixture, RejectsInvalidPathSymbols) {
    Signature s(1, 2);

    EXPECT_THROW((void)s.read_value_at_path(make_path({2})), std::runtime_error);
}

TEST_F(SignatureFixture, TracksCompletionCorrectly) {
    Signature s(1, 2);

    EXPECT_FALSE(s.is_complete());

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("R");

    (void)s.read_value_at_path(make_path({0}));
    s.set_value_at_last_read_node("A");

    (void)s.read_value_at_path(make_path({1}));
    s.set_value_at_last_read_node("B");

    EXPECT_TRUE(s.is_complete());
    EXPECT_EQ(s.missing_count(), 0u);
}

TEST_F(SignatureFixture, FreezeRejectsIncompleteTree) {
    Signature s(1, 2);

    EXPECT_THROW((void)s.freeze(), std::runtime_error);
}

TEST_F(SignatureFixture, FreezeProducesCanonicalSignatureForCompleteTree) {
    Signature s(1, 2);

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("R");

    (void)s.read_value_at_path(make_path({0}));
    s.set_value_at_last_read_node("A");

    (void)s.read_value_at_path(make_path({1}));
    s.set_value_at_last_read_node("B");

    const auto flat = s.freeze();

    EXPECT_FALSE(flat.empty());
    EXPECT_NE(flat.find('R'), std::string::npos);
    EXPECT_NE(flat.find('A'), std::string::npos);
    EXPECT_NE(flat.find('B'), std::string::npos);
}

TEST_F(SignatureFixture, FinalizeProducesFlatSignatureAndLabeledChildren) {
    Signature s(1, 2);

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("R");

    (void)s.read_value_at_path(make_path({0}));
    s.set_value_at_last_read_node("A");

    (void)s.read_value_at_path(make_path({1}));
    s.set_value_at_last_read_node("B");

    auto finalized = std::move(s).finalize_and_split();

    EXPECT_EQ(finalized.output, "R");
    EXPECT_FALSE(finalized.completeSignature.empty());

    ASSERT_EQ(finalized.children.size(), 2u);
    ASSERT_TRUE(finalized.children.contains(0));
    ASSERT_TRUE(finalized.children.contains(1));

    EXPECT_TRUE(finalized.children.at(0));
    EXPECT_TRUE(finalized.children.at(1));
    EXPECT_TRUE(finalized.children.at(0)->has_root());
    EXPECT_TRUE(finalized.children.at(1)->has_root());
}

TEST_F(SignatureFixture, FinalizedChildrenPreserveAlphabetSymbols) {
    Signature s(1, 3);

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("X");

    (void)s.read_value_at_path(make_path({0}));
    s.set_value_at_last_read_node("A");

    (void)s.read_value_at_path(make_path({1}));
    s.set_value_at_last_read_node("B");

    (void)s.read_value_at_path(make_path({2}));
    s.set_value_at_last_read_node("C");

    auto finalized = std::move(s).finalize_and_split();

    ASSERT_EQ(finalized.children.size(), 3u);
    EXPECT_TRUE(finalized.children.contains(0));
    EXPECT_TRUE(finalized.children.contains(1));
    EXPECT_TRUE(finalized.children.contains(2));
}

TEST_F(SignatureFixture, FinalizeRejectsIncompleteTree) {
    Signature s(1, 2);

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("R");

    EXPECT_THROW((void)std::move(s).finalize_and_split(), std::runtime_error);
}

TEST_F(SignatureFixture, MoveConstructionPreservesData) {
    Signature s1(1, 2);

    (void)s1.read_value_at_path(make_path({}));
    s1.set_value_at_last_read_node("R");

    Signature s2(std::move(s1));

    EXPECT_EQ(s2.depth(), 1);
    EXPECT_EQ(s2.alphabet_size(), 2u);
    EXPECT_TRUE(s2.has_root());
}

TEST_F(SignatureFixture, SupportsDeeperTrees) {
    Signature s(2, 2);

    (void)s.read_value_at_path(make_path({}));
    s.set_value_at_last_read_node("R");

    (void)s.read_value_at_path(make_path({0}));
    s.set_value_at_last_read_node("A");

    (void)s.read_value_at_path(make_path({1}));
    s.set_value_at_last_read_node("B");

    (void)s.read_value_at_path(make_path({0, 0}));
    s.set_value_at_last_read_node("C");

    (void)s.read_value_at_path(make_path({0, 1}));
    s.set_value_at_last_read_node("D");

    (void)s.read_value_at_path(make_path({1, 0}));
    s.set_value_at_last_read_node("E");

    (void)s.read_value_at_path(make_path({1, 1}));
    s.set_value_at_last_read_node("F");

    EXPECT_TRUE(s.is_complete());
    EXPECT_NO_THROW({
        const auto flat = s.freeze();
        EXPECT_FALSE(flat.empty());
    });
}