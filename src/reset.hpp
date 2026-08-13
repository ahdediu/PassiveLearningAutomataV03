#pragma once

#include <cstddef>
#include <random>
#include <stdexcept>

#include "core/automaton.hpp"
#include "core/learnerAutomaton.hpp"
#include "core/learningProtocol.hpp"
#include "core/signature.hpp"

class ResetTeacher : public Teacher {
public:
    using StateId = Teacher::StateId;
    using Symbol = Teacher::Symbol;
    using Output = Teacher::Output;

    explicit ResetTeacher(const Automaton& target, unsigned int seed = std::random_device{}())
        : target_(target),
          current_state_(target.initial_state()),
          rng_(seed),
          dist_(0, target.input_count() - 1) {
        if (target.input_count() == 0) {
            throw std::runtime_error("ResetTeacher requires a non-empty alphabet.");
        }
    }

    void reset() {
        current_state_ = target_.initial_state();
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

private:
    const Automaton& target_;
    StateId current_state_{0};
    std::mt19937 rng_;
    std::uniform_int_distribution<Symbol> dist_;
};


class ResetLearner : public Learner {
public:
    using StateId = LearnerAutomaton::StateId;
    using Symbol = Learner::Symbol;
    using Output = Learner::Output;
    using Path = Learner::Path;

    ResetLearner(std::size_t alphabet_size, int signature_depth)
        : automaton_(alphabet_size, signature_depth) {
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
        if (current_path_.size() > automaton_.signature_depth()) {
            return "?";
        }
        const auto& node = automaton_.state(current_state_);
        if (node.status == LearnerAutomaton::StateStatus::Incomplete) {
            return node.incompleteSignature.peek_value_at_path(current_path_);
        }

        return node.output;
    }

    bool learn(const Output& value) override {
        auto& node = automaton_.state(current_state_);
        if (node.status != LearnerAutomaton::StateStatus::Incomplete) return false;

        if (current_path_.size() > automaton_.signature_depth()) return false;

        auto& sig = node.incompleteSignature;
        const Output current = sig.read_value_at_path(current_path_);
        if (current == value) return false;
        if (current != "?") {
            throw std::runtime_error("Signature contradiction in ResetLearner: path leads to " + current + " before, but now " + value);
        }

        sig.set_value_at_last_read_node(value);
        node.output = value;
        return true;
    }

    void advance(Symbol a) override {
        auto& node = automaton_.state(current_state_);
        if (node.status != LearnerAutomaton::StateStatus::Complete) {
            if (current_path_.size() <= automaton_.signature_depth()) {
                current_path_.push_back(a);
            }
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

protected:
    LearnerAutomaton automaton_;
    StateId current_state_{0};
    Path current_path_;
};


class ResetProtocol : public LearningProtocol {
public:
    using StateId = Learner::StateId;

    ResetProtocol(ResetTeacher& teacher, ResetLearner& learner)
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
            throw std::runtime_error("Prediction mistake in ResetProtocol: predicted " + learner_output + " but teacher observed " + teacher_output);
        }

        if (learner_output == "?") {
            ++stats_.queries;
            learner_.learn(teacher_output);
            promote();
            reset_step();
            return;
        }

        advance();
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
        if (teacher_output == learner_output) return false;
        return learner_.learn(teacher_output);
    }

    bool promote() override {
        auto& model = concrete_learner_.automaton();
        const auto id = concrete_learner_.current_state();

        if (id < model.state_count() && model.is_ready_to_finalize(id)) {
            const StateId promoted = model.finalize_state(id);
            if (promoted != id) {
                ++stats_.merges;
            }
            return true;
        }
        return false;
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
    ResetLearner& concrete_learner_;
};
