//
// Created by Adrian Dediu on 05/04/2026.//NOLINT
//
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "signature.hpp"
#include "types.hpp"


/**
 * Learner-side automaton constructed state by state.
 *
 * The LearnerAutomaton stores discovered states together with their transitions.
 * Each state has a stable StateId and can be in one of three statuses:
 *
 *   - Incomplete:
 *       the state still owns a SignatureTree that is being learned.
 *
 *   - Complete:
 *       the state has a canonical completeSignature and outgoing transitions
 *       to successor learner states.
 *
 *   - Merged:
 *       the state was completed, but its canonical signature was found to match
 *       an already existing complete state. In this case, the state becomes
 *       obsolete and mergedInto stores the canonical replacement.
 *
 * Design philosophy:
 *   - learner-state identity is managed here via StateId,
 *   - incomplete signatures are owned by incomplete states,
 *   - complete signatures are used as canonical keys for merge detection,
 *   - transitions are stored by symbol index in input-alphabet order.
 */
class LearnerAutomaton {
public:
	using StateId = core_types::StateId;
	using Symbol = core_types::Symbol;
	using Output = core_types::Output;
	using Path = core_types::Path;

	/**
	 * Sentinel used when a state id is not available.
	 */
	static constexpr StateId invalidStateId = static_cast<StateId>(-1);

	/**
	 * Status of a learner state.
	 */
	enum class StateStatus {
		Incomplete,
		Complete,
		Merged
	};

	/**
	 * Node of the learner automaton.
	 *
	 * ID:
	 *   Stable learner-state identifier.
	 *
	 * Output:
	 *   Output associated with the state. For incomplete states, this may already
	 *   be known from the root of the current signature.
	 *
	 * Status:
	 *   Incomplete, Complete, or Merged.
	 *
	 * incompleteSignature:
	 *   Present only while the state is still being learned.
	 *
	 * completeSignature:
	 *   Canonical frozen signature, meaningful only once the state is complete.
	 *
	 * Transitions:
	 *   Outgoing transitions indexed by symbol. A transition equal to
	 *   invalidStateId means "not yet assigned".
	 *
	 * parentId / parentSymbol:
	 *   Information about the first discovered incoming edge. Useful for
	 *   debugging and for simple merge redirection policies.
	 * mergedInto:
	 *   If status == Merged, this stores the canonical complete state that
	 *   replaced this one.
	 */
	struct AutomatonNode {
        StateId id{invalidStateId};
        Output output{"?"};

        StateStatus status{StateStatus::Incomplete};

        SignatureTree incompleteSignature;
        CompleteSignature completeSignature{};

        std::vector<StateId> transitions;

        StateId parentId{invalidStateId};
        Symbol parentSymbol{0};

        StateId mergedInto{invalidStateId};
    };

private:
	std::vector<AutomatonNode> states_;
	std::unordered_map<CompleteSignature, StateId> signature_to_state_;

	StateId initialState_{invalidStateId};


	std::size_t alphabetSize_{0};
	int signatureDepth_{0};
    mutable std::vector<int8_t> closed_complete_;
    mutable bool closed_complete_valid_{false};

    void invalidate_reachability_cache() const {
        closed_complete_valid_ = false;
    }

public:
	/**
	 * Construct an empty learner automaton description.
	 *
	 * The initial state is not created automatically. Call create_initial_state()
	 * to create state 0 with an incomplete signature.
	 *
	 * @param alphabet_size Number of input symbols.
	 * @param signature_depth Bounded signature depth used for incomplete states.
	 *
	 * @throws std::runtime_error If alphabet_size is zero or signature_depth is negative.
	 */
	LearnerAutomaton(std::size_t alphabet_size, int signature_depth)
		: alphabetSize_(alphabet_size),
		  signatureDepth_(signature_depth) {
		if (alphabetSize_ == 0) {
			throw std::runtime_error("LearnerAutomaton alphabet size must be positive.");
		}
		if (signatureDepth_ < 0) {
			throw std::runtime_error("LearnerAutomaton signature depth cannot be negative.");
		}
	}

	/**
	 * Get the alphabet size.
	 */
	[[nodiscard]] std::size_t alphabet_size() const { return alphabetSize_; }

	/**
	 * Get the signature depth used for incomplete states.
	 */
	[[nodiscard]] int signature_depth() const { return signatureDepth_; }

	/**
	 * Get the number of learner states currently stored.
	 */
	[[nodiscard]] std::size_t state_count() const { return states_.size(); }

	/**
	 * Check whether the initial state has already been created.
	 */
	[[nodiscard]] bool has_initial_state() const {
		return initialState_ != invalidStateId;
	}

	/**
	 * Get the initial state id.
	 *
	 * @throws std::runtime_error If the initial state does not exist.
	 */
	[[nodiscard]] StateId initial_state() const {
		if (!has_initial_state()) {
			throw std::runtime_error("LearnerAutomaton has no initial state.");
		}
		return initialState_;
	}



	/**
	 * Access a learner state by id.
	 *
	 * @param id State identifier.
	 * @return Const reference to the node.
	 *
	 * @throws std::runtime_error If the id is invalid.
	 */
	[[nodiscard]] const AutomatonNode &state(StateId id) const {
		check_state_id(id);
		return states_[id];
	}

	/**
	 * Access a learner state by id.
	 *
	 * @param id State identifier.
	 * @return Mutable reference to the node.
	 *
	 * @throws std::runtime_error If the id is invalid.
	 */
	[[nodiscard]] AutomatonNode &state(StateId id) {
		check_state_id(id);
		return states_[id];
	}

	/**
	 * Create the initial incomplete state.
	 *
	 * The initial state receives id 0.
	 *
	 * @return Initial state id.
	 *
	 * @throws std::runtime_error If the initial state already exists.
	 */
    StateId create_initial_state() {
        if (has_initial_state()) {
            throw std::runtime_error("Initial state already exists.");
        }

        StateId id = create_incomplete_state_impl(invalidStateId, 0);
        initialState_ = id;

        return id;
    }

	/**
	 * Check whether a complete signature is already known.
	 *
	 * @param sig Canonical complete signature.
	 * @return True iff the signature is already present in the complete-state map.
	 */
	[[nodiscard]] bool contains_complete_signature(const CompleteSignature &sig) const {
		return signature_to_state_.contains(sig);
	}

	/**
	 * Get the canonical state id associated with a known complete signature.
	 *
	 * @param sig Canonical complete signature.
	 * @return State id of the canonical complete state.
	 *
	 * @throws std::runtime_error If the signature is not known.
	 */
	[[nodiscard]] StateId state_id_of_complete_signature(const CompleteSignature &sig) const {
		auto it = signature_to_state_.find(sig);
		if (it == signature_to_state_.end()) {
			throw std::runtime_error("Complete signature not found.");
		}
		return it->second;
	}

    [[nodiscard]] bool is_complete(StateId id) const {
        return state(id).status == StateStatus::Complete;
    }

    [[nodiscard]] bool is_incomplete(StateId id) const {
        return state(id).status == StateStatus::Incomplete;
    }

    [[nodiscard]] bool is_merged(StateId id) const {
        return state(id).status == StateStatus::Merged;
    }

    [[nodiscard]] StateId merged_into(StateId id) const {
        const auto& node = state(id);
        if (node.status != StateStatus::Merged) {
            throw std::runtime_error("State is not merged.");
        }
        return node.mergedInto;
    }

	StateId finalize_state(StateId id) {
		check_state_id(id);
        invalidate_reachability_cache();

		if (states_[id].status != StateStatus::Incomplete) {
			throw std::runtime_error("finalize_state requires an incomplete state.");
		}

		auto finalized = std::move(states_[id].incompleteSignature).finalize_and_split();

		const CompleteSignature completeSignature = finalized.completeSignature;
		states_[id].output = finalized.output;
		states_[id].completeSignature = completeSignature;

		auto it = signature_to_state_.find(completeSignature);
		if (it != signature_to_state_.end()) {
			const StateId canonicalId = it->second;
			states_[id].status = StateStatus::Merged;
			states_[id].mergedInto = canonicalId;

			if (states_[id].parentId != invalidStateId) {
				states_[states_[id].parentId].transitions[states_[id].parentSymbol] = canonicalId;
			}

            // Push extra info from finalized subtrees down to canonical state's successors
            for (auto& entry : finalized.children) {
                const Symbol a = entry.first;
                auto& extraTree = entry.second;
                StateId successorId = states_[canonicalId].transitions[a];
                if (successorId != invalidStateId) {
                    // Only push if the successor is incomplete and can accept more info
                    if (states_[successorId].status == StateStatus::Incomplete) {
                        states_[successorId].incompleteSignature.merge_info_from(std::move(*extraTree));
                    }
                }
            }

			return canonicalId;
		}

		signature_to_state_.emplace(completeSignature, id);
		states_[id].status = StateStatus::Complete;
		states_[id].mergedInto = invalidStateId;

		for (auto& entry : finalized.children) {
			const Symbol a = entry.first;
			auto& childTree = entry.second;

			if (!childTree) {
				throw std::runtime_error("finalize_state received a null child tree.");
			}
			if (a >= alphabetSize_) {
				throw std::runtime_error("Child symbol out of range in finalized signature.");
			}

			const StateId childId = create_incomplete_state_impl(id, a);
            states_[childId].incompleteSignature = std::move(*childTree);
            states_[id].transitions[a] = childId;
		}

		return id;
	}
    [[nodiscard]] StateId transition(StateId from, Symbol a) const {
        check_state_id(from);
        check_symbol(a);

        const AutomatonNode& node = states_[from];
        if (node.status != StateStatus::Complete) {
            throw std::runtime_error("Transitions are only valid from complete states.");
        }

        StateId target = node.transitions[a];
        if (target == invalidStateId) {
            throw std::runtime_error("Transition not assigned.");
        }

        return target;
    }

    [[nodiscard]] bool is_ready_to_finalize(StateId id) const {
        const auto& node = state(id);
        return node.status == StateStatus::Incomplete &&
               node.incompleteSignature.is_complete();
    }

    [[nodiscard]] bool is_closed(StateId from) const {
        check_state_id(from);

        if (!closed_complete_valid_ || closed_complete_.size() != states_.size()) {
            update_closed_complete();
        }
        
        return closed_complete_[from] == 1;
    }

    [[nodiscard]] bool is_any_incomplete_reachable(StateId from) const {
        return !is_closed(from);
    }

private:
    void update_closed_complete() const {
        closed_complete_.assign(states_.size(), 1); // Initially all closed
        closed_complete_valid_ = true;
        if (states_.empty()) return;

        std::vector<int8_t> is_open(states_.size(), 0);
        std::vector<std::vector<StateId>> reverse_transitions(states_.size());
        std::queue<StateId> q;

        for (StateId i = 0; i < states_.size(); ++i) {
            const auto& node = states_[i];
            if (node.status == StateStatus::Incomplete) {
                is_open[i] = 1;
                q.push(i);
            }
            
            if (node.status == StateStatus::Complete) {
                for (StateId next : node.transitions) {
                    if (next != invalidStateId) {
                        if (next >= states_.size()) continue;
                        reverse_transitions[next].push_back(i);
                    }
                }
            } else if (node.status == StateStatus::Merged) {
                StateId target = node.mergedInto;
                if (target != invalidStateId && target < states_.size()) {
                    reverse_transitions[target].push_back(i);
                }
            }
        }

        while (!q.empty()) {
            StateId curr = q.front();
            q.pop();
            for (StateId prev : reverse_transitions[curr]) {
                if (is_open[prev] == 0) {
                    is_open[prev] = 1;
                    q.push(prev);
                }
            }
        }

        for (StateId i = 0; i < states_.size(); ++i) {
            closed_complete_[i] = (is_open[i] == 0) ? 1 : 0;
        }
    }

public:

    [[nodiscard]] std::vector<StateId> children_of(StateId id) const {
        const auto& node = state(id);
        if (node.status != StateStatus::Complete) {
            return {};
        }

        std::vector<StateId> result;
        for (StateId child : node.transitions) {
            if (child != invalidStateId) {
                result.push_back(child);
            }
        }
        return result;
    }

private:
    StateId create_incomplete_state_impl(StateId parentId, Symbol parentSymbol) {
        StateId id = states_.size();

        AutomatonNode node;
        node.id = id;
        node.status = StateStatus::Incomplete;
        node.output = "?";
        node.incompleteSignature = SignatureTree(signatureDepth_, alphabetSize_);
        node.transitions.assign(alphabetSize_, invalidStateId);
        node.parentId = parentId;
        node.parentSymbol = parentSymbol;

        states_.push_back(std::move(node));
        invalidate_reachability_cache();
        return id;
    }

    void check_state_id(StateId id) const {
        if (id >= states_.size()) {
            throw std::runtime_error("LearnerAutomaton state id out of range.");
        }
    }

    void check_symbol(Symbol a) const {
        if (a >= alphabetSize_) {
            throw std::runtime_error("LearnerAutomaton symbol out of range.");
        }
    }
};
