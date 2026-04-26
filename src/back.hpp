#pragma once

#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <queue>
#include <deque>

#include "core/automaton.hpp"
#include "core/learnerAutomaton.hpp"
#include "core/learningProtocol.hpp"
#include "core/signature.hpp"

class BackTeacher : public Teacher {
public:
    using StateId = Teacher::StateId;
    using Symbol = Teacher::Symbol;
    using Output = Teacher::Output;

    explicit BackTeacher(const Automaton& target, unsigned int seed = std::random_device{}())
        : target_(target),
          current_state_(target.initial_state()),
          rng_(seed),
          dist_(0, target.input_count() - 1) {
        if (target.input_count() == 0) {
            throw std::runtime_error("BackTeacher requires a non-empty alphabet.");
        }
    }

    [[nodiscard]] Output observe() const override {
        return target_.output(current_state_);
    }

    [[nodiscard]] Symbol next_symbol() override {
        return dist_(rng_);
    }

    void advance(Symbol a) override {
        current_state_ = target_.next_state(a, current_state_);
    }

    [[nodiscard]] StateId current_state() const override {
        return current_state_;
    }

    void move_to_state(StateId r) {
        current_state_ = r;
    }

private:
    const Automaton& target_;
    StateId current_state_{0};
    mutable std::mt19937 rng_;
    mutable std::uniform_int_distribution<Symbol> dist_;
};

class BackLearner : public Learner {
public:
    using StateId = LearnerAutomaton::StateId;
    using Symbol = Learner::Symbol;
    using Output = Learner::Output;
    using Path = Learner::Path;

    BackLearner(std::size_t alphabet_size, int signature_depth, bool detect_loops = true)
        : automaton_(alphabet_size, signature_depth), detect_loops_(detect_loops) {
        automaton_.create_initial_state();
        current_state_ = automaton_.initial_state();
        current_path_.clear();
    }

    void normalize() {
        while (!current_path_.empty()) {
            if (automaton_.is_merged(current_state_)) {
                current_state_ = automaton_.merged_into(current_state_);
                continue;
            }
            if (automaton_.is_complete(current_state_)) {
                Symbol a = current_path_.front();
                current_state_ = automaton_.transition(current_state_, a);
                current_path_.erase(current_path_.begin());
            } else {
                break;
            }
        }
        if (automaton_.is_merged(current_state_)) {
            current_state_ = automaton_.merged_into(current_state_);
        }
    }

    [[nodiscard]] Output observe() const override {
        const auto& node = automaton_.state(current_state_);
        if (node.status == LearnerAutomaton::StateStatus::Incomplete) {
            Output pred = node.incompleteSignature.peek_value_at_path(current_path_);
            if (pred == "?") {
                return "?";
            }
            if (!node.incompleteSignature.has_missing_leaf_below(current_path_)) {
                return "!" + pred;
            }
            return pred;
        }

        if (automaton_.is_closed(current_state_)) {
            return "!" + node.output;
        }

        return node.output;
    }

    bool learn(const Output& value) override {
        auto& node = automaton_.state(current_state_);
        if (node.status != LearnerAutomaton::StateStatus::Incomplete) {
            return false; 
        }

        auto& sig = node.incompleteSignature;
        const Output current = sig.read_value_at_path(current_path_);
        if (current == value) {
            return false;
        }
        if (current != "?") {
            throw std::runtime_error("Signature contradiction in BackLearner: path leads to " + current + " before, but now " + value);
        }

        sig.set_value_at_last_read_node(value);
        node.output = value;
        return true;
    }

    void advance(Symbol a) override {
        auto& node = automaton_.state(current_state_);
        if (node.status != LearnerAutomaton::StateStatus::Complete) {
            current_path_.push_back(a);
            return;
        }
        current_state_ = automaton_.transition(current_state_, a);
    }

    [[nodiscard]] bool stopCondition() const override {
        return incomplete_state_count() == 0;
    }

    [[nodiscard]] std::size_t state_count() const override {
        return automaton_.state_count();
    }

    [[nodiscard]] std::size_t incomplete_state_count() const override {
        std::size_t count = 0;
        const auto sc = automaton_.state_count();
        for (StateId i = 0; i < sc; ++i) {
            if (automaton_.state(i).status == LearnerAutomaton::StateStatus::Incomplete) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] StateId current_state() const override {
        return current_state_;
    }

    [[nodiscard]] const Path& current_path() const override {
        return current_path_;
    }

    [[nodiscard]] bool current_state_complete() const override {
        return automaton_.state(current_state_).status == LearnerAutomaton::StateStatus::Complete;
    }

    [[nodiscard]] LearnerAutomaton& automaton() {
        return automaton_;
    }

    [[nodiscard]] const LearnerAutomaton& automaton() const {
        return automaton_;
    }

    void set_current_state(StateId id) {
        current_state_ = id;
        current_path_.clear();
    }

    void set_state_and_path(StateId q, const Path& p) {
        current_state_ = q;
        current_path_ = p;
    }

private:
    LearnerAutomaton automaton_;
    StateId current_state_{0};
    Path current_path_;
    bool detect_loops_;
};

class BackProtocol : public LearningProtocol {
public:
    using StateId = LearnerAutomaton::StateId;
    using Symbol  = Learner::Symbol;
    using Output  = Learner::Output;
    using Path    = Learner::Path;

    BackProtocol(BackTeacher& teacher, BackLearner& learner)
        : LearningProtocol(teacher, learner), back_teacher_(teacher), back_learner_(learner) {}

    void reset() override {
        teacher_stack_.clear();
        learner_stack_.clear();
    }

    void step() override {
        ++stats_.trials;
        back_learner_.normalize();
        
        Output predicted = back_learner_.observe();
        Output teacher_observed = back_teacher_.observe();

        if (predicted.starts_with("!")) {
            Output actual_pred = predicted.substr(1);
            if (actual_pred != teacher_observed) {
                throw std::runtime_error("Prediction mistake in BackProtocol (backtracking): predicted " + actual_pred + " but teacher observed " + teacher_observed);
            }
            ++stats_.backs;
            if (back_learner_.automaton().state(back_learner_.current_state()).status == LearnerAutomaton::StateStatus::Incomplete) {
                promote();
            }
            move_back(1);
            return;
        }

        if (predicted != "?" && predicted != teacher_observed) {
            throw std::runtime_error("Prediction mistake in BackProtocol: predicted " + predicted + " but teacher observed " + teacher_observed);
        }

        if (predicted == "?") {
            stats_.queries++;
            if (back_learner_.learn(teacher_observed)) {
                promote();
            }
            
            move_back(1);
        } else {
            Symbol a = back_teacher_.next_symbol();

            teacher_stack_.push_back(back_teacher_.current_state());
            learner_stack_.push_back({back_learner_.current_state(), back_learner_.current_path()});

            back_teacher_.advance(a);
            back_learner_.advance(a);
        }
    }

protected:
    bool observe(Teacher::Output&, Learner::Output&) override { return false; }
    bool validate_and_update(const Teacher::Output&, const Learner::Output&) override { return false; }
    
    bool promote() override {
        auto& model = back_learner_.automaton();
        std::queue<StateId> to_check;
        to_check.push(back_learner_.current_state());
        bool any_promoted = false;

        while (!to_check.empty()) {
            const StateId id = to_check.front();
            to_check.pop();

            if (id < model.state_count() && model.is_ready_to_finalize(id)) {
                const StateId promoted = model.finalize_state(id);
                any_promoted = true;
                if (promoted != id) {
                    ++stats_.merges;
                }
                
                update_stacks_after_finalize(id, promoted);

                if (promoted == id) {
                    for (StateId child : model.children_of(promoted)) {
                        to_check.push(child);
                    }
                }
            }
        }
        if (any_promoted) {
            back_learner_.normalize();
        }
        return any_promoted;
    }
    void advance() override {}
    void reset_step() override {}

private:
    void move_back(std::size_t n) {
        std::size_t steps = std::min(n, teacher_stack_.size());
        for (std::size_t i = 0; i < steps; ++i) {
            auto t_pos = teacher_stack_.back();
            teacher_stack_.pop_back();
            auto l_pos = learner_stack_.back();
            learner_stack_.pop_back();
            
            if (i == steps - 1) {
                back_teacher_.move_to_state(t_pos);
                back_learner_.set_state_and_path(l_pos.first, l_pos.second);
            }
        }
    }

    void update_stacks_after_finalize(StateId old_q, StateId new_q) {
        // Python logic:
        // if q1 == self.q:
        //     q1 = r0
        //     while len(p1) > 0 and q1 in self.Q:
        //         c, p2 = p1[0], p1[1:]
        //         q1 = self.transitions[int(c)][q1]
        //         p1 = p2
        
        for (auto& entry : learner_stack_) {
            StateId& q = entry.first;
            Path& p = entry.second;
            
            if (q == old_q) {
                q = new_q;
                while (!p.empty() && back_learner_.automaton().is_complete(q)) {
                    Symbol a = p.front();
                    p.erase(p.begin());
                    q = back_learner_.automaton().transition(q, a);
                }
            }
        }
        
        // Also handle the case where q was promoted to new incomplete states
        // In Python: qx = self.id_new_state; self.transitions[a][self.q] = qx
        // if q1 == self.q and c == a: temp_stack.append((qx, p2))
        
        // In our C++, finalize_state creates multiple children. 
        // The Python code does it one by one in a loop over alphabet? No, finalize_state in C++ creates all children.
        // Wait, the Python code's `correct_answer` seems to do promotion and stack update in one go.
        
        // Our finalize_state(q_l) returns:
        // - canonicalId if merged (already handled above if result_id != old_q)
        // - old_q if promoted (result_id == old_q)
        
        if (new_q == old_q) {
             // Promotion case. 
             // In Python, it replaces (q, ap') with (qx, p') where q --a--> qx
             // We need to check if any stack entry has q == old_q and p starts with some 'a'.
             for (auto& entry : learner_stack_) {
                 StateId& q = entry.first;
                 Path& p = entry.second;
                 if (q == old_q && !p.empty()) {
                     Symbol a = p.front();
                     p.erase(p.begin());
                     q = back_learner_.automaton().transition(old_q, a);
                     
                     // And possibly continue if it's now complete? 
                     // Usually newly created children are Incomplete, so we stop there.
                 }
             }
        }
    }

    BackTeacher& back_teacher_;
    BackLearner& back_learner_;
    std::deque<Teacher::StateId> teacher_stack_;
    std::deque<std::pair<StateId, Path>> learner_stack_;
};
