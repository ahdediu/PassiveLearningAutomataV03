#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "types.hpp"

using CompleteSignature = std::string;

class SignatureTree {
public:
    using Symbol = core_types::Symbol;
    using Output = core_types::Output;
    using Path = core_types::Path;

    struct FinalizedSignatureNode {
        Output output;
        CompleteSignature completeSignature;
        std::map<Symbol, std::unique_ptr<SignatureTree>> children;
    };

private:
    struct Node {
        Output value{"?"};
        std::vector<std::unique_ptr<Node>> children;

        explicit Node(std::size_t alphabet_size)
            : children(alphabet_size) {}
    };

    int depth_{0};
    std::size_t alphabet_size_{0};
    std::unique_ptr<Node> root_;
    std::size_t missing_count_{0};
    std::size_t missing_leaf_paths_{0};

    Node* last_accessed_node_{nullptr};
    bool last_accessed_bounded_{false};
    bool last_accessed_is_leaf_{false};

public:
    SignatureTree() = default;

    SignatureTree(int depth, std::size_t alphabet_size)
        : depth_(depth),
          alphabet_size_(alphabet_size) {
        if (depth_ < 0) {
            throw std::runtime_error("Signature depth cannot be negative.");
        }
        if (alphabet_size_ == 0) {
            throw std::runtime_error("Signature alphabet size must be positive.");
        }

        root_ = std::make_unique<Node>(alphabet_size_);
        missing_count_ = expected_bounded_node_count(depth_, alphabet_size_);
        missing_leaf_paths_ = expected_leaf_count(depth_, alphabet_size_);
    }

    [[nodiscard]] bool has_root() const { return static_cast<bool>(root_); }
    [[nodiscard]] int depth() const { return depth_; }
    [[nodiscard]] std::size_t alphabet_size() const { return alphabet_size_; }
    [[nodiscard]] std::size_t missing_count() const { return missing_count_; }
    [[nodiscard]] std::size_t missing_leaf_paths() const { return missing_leaf_paths_; }

    [[nodiscard]] bool is_complete() const {
        return has_root() && missing_count_ == 0;
    }

    [[nodiscard]] bool has_missing_leaf_below(const Path& path) const {
        const Node* node = get_node(path);
        return count_missing_leaves_rec(node, static_cast<int>(path.size())) > 0;
    }

    [[nodiscard]] CompleteSignature freeze() const {
        if (!is_complete()) {
            throw std::runtime_error("Cannot freeze an incomplete signature.");
        }
        return serialize_bounded_rec(root_.get(), 0);
    }

    [[nodiscard]] FinalizedSignatureNode finalize_and_split() && {
        if (!is_complete()) {
            throw std::runtime_error("Cannot finalize an incomplete signature.");
        }

        FinalizedSignatureNode result;
        result.output = root_->value;
        result.completeSignature = serialize_bounded_rec(root_.get(), 0);

        for (Symbol a = 0; a < alphabet_size_; ++a) {
            auto child_tree = std::make_unique<SignatureTree>(depth_, alphabet_size_);

            if (root_->children[a]) {
                child_tree->root_ = std::move(root_->children[a]);
                child_tree->missing_count_ = child_tree->count_missing_bounded_nodes();
                child_tree->missing_leaf_paths_ = child_tree->count_missing_leaves_rec(child_tree->root_.get(), 0);
            } else {
                child_tree->root_ = std::make_unique<Node>(alphabet_size_);
                child_tree->missing_count_ = expected_bounded_node_count(depth_, alphabet_size_);
            }

            child_tree->invalidate_last_access_cache();
            result.children.emplace(a, std::move(child_tree));
        }

        invalidate_last_access_cache();
        return result;
    }

public:
    [[nodiscard]] Output peek_value_at_path(const Path& path) const {
        const Node* node = get_node(path);
        return node ? node->value : "?";
    }

    [[nodiscard]] Output read_value_at_path(const Path& path) {
        Node* node = get_or_create_node(path);
        last_accessed_node_ = node;
        last_accessed_bounded_ = static_cast<int>(path.size()) <= depth_;
        last_accessed_is_leaf_ = static_cast<int>(path.size()) == depth_;
        return node->value;
    }

    void set_value_at_last_read_node(const Output& value) {
        if (!last_accessed_node_) {
            throw std::runtime_error("No last accessed node to update.");
        }

        Node* current = last_accessed_node_;
        const bool was_unknown = (current->value == "?");
        const bool becomes_known = (value != "?");

        current->value = value;

        if (last_accessed_bounded_ && was_unknown && becomes_known) {
            if (missing_count_ == 0) {
                throw std::runtime_error("Signature missing_count_ underflow.");
            }
            --missing_count_;
        }

        if (last_accessed_is_leaf_ && was_unknown && becomes_known) {
            if (missing_leaf_paths_ == 0) {
                throw std::runtime_error("Signature missing_leaf_paths_ underflow.");
            }
            --missing_leaf_paths_;
        }
    }

    void set_root_value(const Output& value) {
        if (!root_) {
            throw std::runtime_error("Signature tree has no root.");
        }

        const bool was_unknown = (root_->value == "?");
        const bool becomes_known = (value != "?");

        root_->value = value;

        if (was_unknown && becomes_known) {
            if (missing_count_ == 0) {
                throw std::runtime_error("Signature missing_count_ underflow.");
            }
            --missing_count_;

            if (depth_ == 0) {
                if (missing_leaf_paths_ == 0) {
                    throw std::runtime_error("Signature missing_leaf_paths_ underflow.");
                }
                --missing_leaf_paths_;
            }
        }

        last_accessed_node_ = root_.get();
        last_accessed_bounded_ = true;
        last_accessed_is_leaf_ = (depth_ == 0);
    }

    void merge_info_from(SignatureTree&& other) {
        if (!other.root_) return;
        if (!root_) {
            *this = std::move(other);
            return;
        }
        merge_nodes_rec(root_.get(), other.root_.get(), 0);
        missing_count_ = count_missing_bounded_nodes();
        missing_leaf_paths_ = count_missing_leaves_rec(root_.get(), 0);
        invalidate_last_access_cache();
    }

private:
    void merge_nodes_rec(Node* target, Node* source, int level) {
        if (source->value != "?") {
            if (target->value != "?" && target->value != source->value) {
                throw std::runtime_error("Signature contradiction during merge: target=" + target->value + ", source=" + source->value);
            }
            target->value = source->value;
        }

        for (Symbol a = 0; a < alphabet_size_; ++a) {
            if (source->children[a]) {
                if (!target->children[a]) {
                    target->children[a] = std::move(source->children[a]);
                } else {
                    merge_nodes_rec(target->children[a].get(), source->children[a].get(), level + 1);
                }
            }
        }
    }

private:
    void invalidate_last_access_cache() {
        last_accessed_node_ = nullptr;
        last_accessed_bounded_ = false;
        last_accessed_is_leaf_ = false;
    }

    [[nodiscard]] static std::size_t
    expected_leaf_count(int depth, std::size_t alphabet_size) {
        if (depth < 0) return 0;
        std::size_t count = 1;
        for (int i = 0; i < depth; ++i) {
            count *= alphabet_size;
        }
        return count;
    }

    [[nodiscard]] static std::size_t
    expected_bounded_node_count(int depth, std::size_t alphabet_size) {
        if (alphabet_size == 0) {
            throw std::runtime_error("Alphabet size must be positive.");
        }

        std::size_t total = 0;
        std::size_t current_level_count = 1;
        for (int i = 0; i <= depth; ++i) {
            total += current_level_count;
            current_level_count *= alphabet_size;
        }
        return total;
    }

    void ensure_root() const {
        if (!root_) {
            throw std::runtime_error("Signature tree has no root.");
        }
    }

    void check_symbol(Symbol a) const {
        if (a >= alphabet_size_) {
            throw std::runtime_error("Signature symbol out of range.");
        }
    }

    [[nodiscard]] const Node* get_node(const Path& path) const {
        if (!root_) return nullptr;
        const Node* current = root_.get();
        for (Symbol a : path) {
            if (a >= alphabet_size_) return nullptr;
            if (!current->children[a]) return nullptr;
            current = current->children[a].get();
        }
        return current;
    }

    [[nodiscard]] Node* get_or_create_node(const Path& path) {
        ensure_root();

        Node* current = root_.get();
        for (Symbol a : path) {
            check_symbol(a);
            if (!current->children[a]) {
                current->children[a] = std::make_unique<Node>(alphabet_size_);
            }
            current = current->children[a].get();
        }
        return current;
    }

    [[nodiscard]] std::size_t count_missing_node_rec(const Node* node, int level) const {
        if (level > depth_) {
            return 0;
        }

        if (!node) {
            return expected_bounded_node_count(depth_ - level, alphabet_size_);
        }

        std::size_t missing = (node->value == "?") ? 1u : 0u;

        if (level < depth_) {
            for (Symbol a = 0; a < alphabet_size_; ++a) {
                missing += count_missing_node_rec(node->children[a].get(), level + 1);
            }
        }
        return missing;
    }

    [[nodiscard]] std::size_t count_missing_leaves_rec(const Node* node, int level) const {
        if (level > depth_) {
            return 0;
        }
        if (level == depth_) {
            return (!node || node->value == "?") ? 1u : 0u;
        }
        if (!node) {
            return expected_leaf_count(depth_ - level, alphabet_size_);
        }
        std::size_t count = 0;
        for (Symbol a = 0; a < alphabet_size_; ++a) {
            count += count_missing_leaves_rec(node->children[a].get(), level + 1);
        }
        return count;
    }

    [[nodiscard]] std::size_t count_missing_bounded_nodes() const {
        if (!root_) {
            return 0;
        }
        return count_missing_node_rec(root_.get(), 0);
    }

    [[nodiscard]] CompleteSignature serialize_bounded_rec(const Node* node, int level) const {
        if (!node) {
            throw std::runtime_error("Cannot serialize a missing node.");
        }
        if (node->value == "?") {
            throw std::runtime_error("Cannot serialize an unknown node value.");
        }

        std::string result = "(" + node->value;

        if (level < depth_) {
            for (Symbol a = 0; a < alphabet_size_; ++a) {
                if (!node->children[a]) {
                    throw std::runtime_error("Cannot serialize incomplete subtree.");
                }
                result += serialize_bounded_rec(node->children[a].get(), level + 1);
            }
        }

        result += ")";
        return result;
    }
};