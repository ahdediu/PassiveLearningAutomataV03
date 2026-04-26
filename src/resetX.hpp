#pragma once

#include <cstddef>
#include <queue>
#include <random>
#include <stdexcept>

#include "core/automaton.hpp"
#include "core/learnerAutomaton.hpp"
#include "core/learningProtocol.hpp"
#include "core/signature.hpp"
#include "reset.hpp"

class ResetXLearner : public Learner {
public:
    using StateId = LearnerAutomaton::StateId;
    using Symbol = Learner::Symbol;
    using Output = Learner::Output;
    using Path = Learner::Path;

    ResetXLearner(std::size_t alphabet_size, int signature_depth, bool detect_loops = false)
        : automaton_(alphabet_size, signature_depth), detect_loops_(detect_loops) {
        automaton_.create_initial_state();
        current_state_ = automaton_.initial_state();
        current_path_.clear();
    }

    void reset() {
        current_state_ = automaton_.initial_state();
        while (automaton_.is_merged(current_state_)) {
            current_state_ = automaton_.merged_into(current_state_);
        }
        current_path_.clear();
    }

    [[nodiscard]] Output observe() const override {
        const auto& node = automaton_.state(current_state_);
        if (node.status == LearnerAutomaton::StateStatus::Incomplete) {
            // Predict based on current path in the signature tree
            return node.incompleteSignature.peek_value_at_path(current_path_);
        }
        
        // Loop detection: if we can't reach any incomplete state, signal "?"
        // so that the protocol can reset/backtrack.
        if (detect_loops_ && !automaton_.is_any_incomplete_reachable(current_state_)) {
            return "?";
        }

        return node.output;
    }

    bool learn(const Output& value) override {
        auto& node = automaton_.state(current_state_);

        if (node.status != LearnerAutomaton::StateStatus::Incomplete) {
            return false; // Already finalized or stuck signal
        }

        auto& sig = node.incompleteSignature;
        const Output current = sig.read_value_at_path(current_path_);
        if (current == value) {
            return false;
        }
        if (current != "?") {
            throw std::runtime_error("Signature contradiction in ResetXLearner: path leads to " + current + " before, but now " + value);
        }

        sig.set_value_at_last_read_node(value);
        //node.output = value;
        return true;
    }

    [[nodiscard]] int signature_depth() const {
        return automaton_.signature_depth();
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
        for (StateId i = 0; i < automaton_.state_count(); ++i) {
            if (automaton_.state(i).status == LearnerAutomaton::StateStatus::Incomplete) {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] bool current_state_complete() const override {
        return automaton_.state(current_state_).status == LearnerAutomaton::StateStatus::Complete;
    }

    [[nodiscard]] StateId current_state() const override {
        return current_state_;
    }

    [[nodiscard]] const Path& current_path() const override {
        return current_path_;
    }

    [[nodiscard]] LearnerAutomaton& automaton() {
        return automaton_;
    }

    [[nodiscard]] const LearnerAutomaton& automaton() const {
        return automaton_;
    }

    void set_current_state(StateId id) {
        current_state_ = id;
    }

    void set_current_path(const Path& path) {
        current_path_ = path;
    }

private:
    LearnerAutomaton automaton_;
    StateId current_state_{0};
    Path current_path_;
    bool detect_loops_;
};

class ResetXProtocol : public LearningProtocol {
public:
    using StateId = Learner::StateId;

    ResetXProtocol(ResetTeacher& teacher, ResetXLearner& learner)
        : LearningProtocol(teacher, learner),
          concrete_teacher_(teacher),
          concrete_learner_(learner) {}

    void reset() override {
        concrete_learner_.reset();
        concrete_teacher_.reset();
    }

    void step() override {
        ++stats_.trials;

        Teacher::Output teacher_output{};
        Learner::Output learner_output{};

        if (!observe(teacher_output, learner_output)) {
            throw std::runtime_error("Observation failed.");
        }

        if (learner_output != "?" && learner_output != teacher_output) {
            throw std::runtime_error("Prediction mistake in ResetXProtocol: predicted " + learner_output + " but teacher observed " + teacher_output);
        }

        if (learner_output == "?") {
            ++stats_.queries;
            learner_.learn(teacher_output);
            promote();
            reset_step();
            return;
        }

        if (promote()) {
            reset_step();
        } else {
            advance();
        }
    }

protected:
    [[nodiscard]] bool observe(Teacher::Output& teacher_output,
                               Learner::Output& learner_output) override {
        teacher_output = teacher_.observe();
        learner_output = learner_.observe();
        return true;
    }

    [[nodiscard]] bool validate_and_update(const Teacher::Output& teacher_output,
                                           const Learner::Output& learner_output) override {
        if (teacher_output == learner_output) {
            return false;
        }
        return learner_.learn(teacher_output);
    }

    bool promote() override {
        auto& model = concrete_learner_.automaton();
        std::queue<StateId> to_check;
        to_check.push(concrete_learner_.current_state());
        bool any_promoted = false;

        while (!to_check.empty()) {
            const StateId id = to_check.front();
            to_check.pop();

            if (id >= model.state_count()) {
                continue;
            }

            if (model.is_ready_to_finalize(id)) {
                const StateId promoted = model.finalize_state(id);
                any_promoted = true;
                if (promoted != id) {
                    ++stats_.merges;
                } else {
                    for (StateId child : model.children_of(promoted)) {
                        to_check.push(child);
                    }
                }
            }
        }
        return any_promoted;
    }

    void advance() override {
        const auto symbol = teacher_.next_symbol();
        teacher_.advance(symbol);
        concrete_learner_.advance(symbol);
    }

    void reset_step() override {
        concrete_teacher_.reset();
        concrete_learner_.reset();
    }

private:
    ResetTeacher& concrete_teacher_;
    ResetXLearner& concrete_learner_;
};
