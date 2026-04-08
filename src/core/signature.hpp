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

    Node* last_accessed_node_{nullptr};
    bool last_accessed_bounded_{false};

public:
    SignatureTree() = default;

    SignatureTree(int depth, std::size_t alphabet_size)
        : depth_(depth),
          alphabet_size_(alphabet_size),
          root_(nullptr),
          missing_count_(0),
          last_accessed_node_(nullptr),
          last_accessed_bounded_(false) {
        if (depth_ < 0) {
            throw std::runtime_error("Signature depth cannot be negative.");
        }
        if (alphabet_size_ == 0) {
            throw std::runtime_error("Signature alphabet size must be positive.");
        }

        root_ = std::make_unique<Node>(alphabet_size_);
        missing_count_ = expected_bounded_node_count(depth_, alphabet_size_);
    }

    SignatureTree(SignatureTree&& other) noexcept
        : depth_(other.depth_),
          alphabet_size_(other.alphabet_size_),
          root_(std::move(other.root_)),
          missing_count_(other.missing_count_),
          last_accessed_node_(nullptr),
          last_accessed_bounded_(false) {
        other.invalidate_last_access_cache();
    }

    SignatureTree& operator=(SignatureTree&& other) noexcept {
        if (this != &other) {
            depth_ = other.depth_;
            alphabet_size_ = other.alphabet_size_;
            root_ = std::move(other.root_);
            missing_count_ = other.missing_count_;
            invalidate_last_access_cache();
            other.invalidate_last_access_cache();
        }
        return *this;
    }

    SignatureTree(const SignatureTree&) = delete;
    SignatureTree& operator=(const SignatureTree&) = delete;

    [[nodiscard]] int depth() const { return depth_; }
    [[nodiscard]] std::size_t alphabet_size() const { return alphabet_size_; }
    [[nodiscard]] bool has_root() const { return static_cast<bool>(root_); }
    [[nodiscard]] std::size_t missing_count() const { return missing_count_; }
    [[nodiscard]] bool is_complete() const { return missing_count_ == 0; }

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
            if (!root_->children[a]) {
                throw std::runtime_error("Complete signature missing child subtree.");
            }

            auto child_tree = std::make_unique<SignatureTree>(depth_, alphabet_size_);
            child_tree->root_ = std::move(root_->children[a]);
            child_tree->missing_count_ = child_tree->count_missing_bounded_nodes();
            child_tree->invalidate_last_access_cache();

            result.children.emplace(a, std::move(child_tree));
        }

        invalidate_last_access_cache();
        return result;
    }

private:
    void invalidate_last_access_cache() {
        last_accessed_node_ = nullptr;
        last_accessed_bounded_ = false;
    }

    [[nodiscard]] static std::size_t
    expected_bounded_node_count(int depth, std::size_t alphabet_size) {
        if (alphabet_size == 0) {
            throw std::runtime_error("Alphabet size must be positive.");
        }

        std::size_t total = 0;
        std::size_t power = 1;

        for (int i = 0; i <= depth; ++i) {
            total += power;
            power *= alphabet_size;
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

    [[nodiscard]] std::size_t count_missing_rec(const Node* node, int level) const {
        if (level > depth_) {
            return 0;
        }

        if (!node) {
            return expected_bounded_node_count(depth_ - level, alphabet_size_);
        }

        std::size_t missing = (node->value == "?") ? 1u : 0u;

        if (level == depth_) {
            return missing;
        }

        for (Symbol a = 0; a < alphabet_size_; ++a) {
            missing += count_missing_rec(node->children[a].get(), level + 1);
        }

        return missing;
    }

    [[nodiscard]] std::size_t count_missing_bounded_nodes() const {
        if (!root_) {
            return 0;
        }
        return count_missing_rec(root_.get(), 0);
    }

    [[nodiscard]] CompleteSignature
    serialize_bounded_rec(const Node* node, int level) const {
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

public:
    [[nodiscard]] Output read_value_at_path(const Path& path) {
        Node* node = get_or_create_node(path);
        last_accessed_node_ = node;
        last_accessed_bounded_ = static_cast<int>(path.size()) <= depth_;
        return node->value;
    }

    void set_value_at_last_read_node(const Output& value) {
        if (!last_accessed_node_) {
            throw std::runtime_error("No last accessed node to update.");
        }

        Node* current = last_accessed_node_;
        const bool bounded = last_accessed_bounded_;
        const bool was_unknown = (current->value == "?");
        const bool becomes_known = (value != "?");

        current->value = value;

        if (bounded && was_unknown && becomes_known) {
            if (missing_count_ == 0) {
                throw std::runtime_error("Signature missing_count_ underflow.");
            }
            --missing_count_;
        }
    }
};