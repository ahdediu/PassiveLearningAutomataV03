#pragma once

#include <cstddef>
#include <string>
#include <functional>

#include "types.hpp"

class Teacher {
public:
    using StateId = core_types::StateId;
    using Symbol  = core_types::Symbol;
    using Output  = core_types::Output;

    virtual ~Teacher() = default;

    /**
     * Observe the teacher's current output.
     */
    [[nodiscard]] virtual Output observe() const = 0;

    /**
     * Choose the next input symbol to drive the synchronized advance.
     */
    [[nodiscard]] virtual Symbol next_symbol() = 0;

    /**
     * Advance the teacher using the chosen symbol.
     */
    virtual void advance(Symbol a) = 0;

    /**
     * Optional debugging helper.
     */
    [[nodiscard]] virtual StateId current_state() const = 0;
};

class Learner {
public:
    using StateId = core_types::StateId;
    using Symbol  = core_types::Symbol;
    using Output  = core_types::Output;
    using Path    = core_types::Path;

    virtual ~Learner() = default;

    /**
     * Observe what the learner currently predicts.
     */
    [[nodiscard]] virtual Output observe() const = 0;

    /**
     * Incorporate the teacher's observed output into the learner.
     * Returns true if the learner state changed.
     */
    virtual bool learn(const Output& value) = 0;

    /**
     * Advance the learner using the same symbol chosen by the teacher.
     */
    virtual void advance(Symbol a) = 0;

    /**
     * Return true when the learning process should stop.
     */
    [[nodiscard]] virtual bool stopCondition() const = 0;

    /**
     * Number of learner states currently stored.
     */
    [[nodiscard]] virtual std::size_t state_count() const = 0;

    /**
     * Number of currently incomplete learner states.
     */
    [[nodiscard]] virtual std::size_t incomplete_state_count() const = 0;

    /**
     * Current learner state id.
     */
    [[nodiscard]] virtual StateId current_state() const = 0;

    /**
     * Current path within the learner's active signature/tree.
     */
    [[nodiscard]] virtual const Path& current_path() const = 0;

    /**
     * True if the current learner state/signature is complete.
     */
    [[nodiscard]] virtual bool current_state_complete() const = 0;
};

struct LearningStatistics {
    std::size_t trials{0};           // Total protocol rounds (Observe, confront, move)
    std::size_t queries{0};          // prediction errors (?)
    std::size_t backs{0};            // backtracking signals (!)
    std::size_t merges{0};           // Total number of merged states (m)
    std::size_t complete_states{0};  // Number of complete states (s)
    std::size_t incomplete_states{0};// Number of incomplete states (i)
};

class LearningProtocol {
public:
    LearningProtocol(Teacher& teacher, Learner& learner)
        : teacher_(teacher), learner_(learner) {}

    virtual ~LearningProtocol() = default;

    /**
     * Reset both teacher and learner.
     */
    virtual void reset() {
    }

    /**
     * Perform one protocol step:
     * 1) observe
     * 2) validate/update
     * 3) promote/merge/create if needed
     * 4) advance
     */
    virtual void step() = 0;

    /**
     * Run until the learner reports completion.
     */
    virtual void run() {
        reset();

        while (!learner_.stopCondition()) {
            step();
            refresh_statistics();
            if (progress_callback_) {
                progress_callback_(stats_);
            }
        }
    }

    /**
     * Access collected statistics.
     */
    [[nodiscard]] const LearningStatistics& statistics() const {
        return stats_;
    }

    using ProgressCallback = std::function<void(const LearningStatistics&)>;
    void set_progress_callback(ProgressCallback cb) { progress_callback_ = std::move(cb); }

protected:
    /**
     * Observe teacher and learner outputs.
     */
    [[nodiscard]] virtual bool observe(Teacher::Output& teacher_output,
                                       Learner::Output& learner_output) = 0;

    /**
     * Validate prediction and update the learner if needed.
     * Returns true if the learner learned something new.
     */
    [[nodiscard]] virtual bool validate_and_update(const Teacher::Output& teacher_output,
                                                   const Learner::Output& learner_output) = 0;

    /**
     * Promote a completed learner state, possibly merging it.
     * Returns true if any state was finalized (complete or merged).
     */
    virtual bool promote() = 0;

    /**
     * Advance teacher and learner synchronously.
     */
    virtual void advance() = 0;

    /**
     * Reset both sides in a protocol-specific way.
     */
    virtual void reset_step() = 0;

    /**
     * Refresh protocol statistics from learner state.
     */
    virtual void refresh_statistics() {
        stats_.incomplete_states = learner_.incomplete_state_count();
        stats_.complete_states = learner_.state_count() - stats_.incomplete_states - stats_.merges;
    }

protected:
    Teacher& teacher_;
    Learner& learner_;
    LearningStatistics stats_{};
    ProgressCallback progress_callback_;
};