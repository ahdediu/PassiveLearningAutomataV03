//
// Created by Adrian Dediu on 05/04/2026.//NOLINT
//
#pragma once

#include <cstddef>
#include <optional>
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
		Output output{};

		StateStatus status{StateStatus::Incomplete};

		std::optional<SignatureTree> incompleteSignature;
		CompleteSignature completeSignature{};

		std::vector<StateId> transitions;

		StateId parentId{invalidStateId};
		Symbol parentSymbol{0};

		StateId mergedInto{invalidStateId};
	};

private:
	std::vector<AutomatonNode> states_;
	std::unordered_map<CompleteSignature, StateId> completeSignatureToStateId_;

	StateId initialState_{invalidStateId};


	std::size_t alphabetSize_{0};
	int signatureDepth_{0};

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
		return completeSignatureToStateId_.contains(sig);
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
		auto it = completeSignatureToStateId_.find(sig);
		if (it == completeSignatureToStateId_.end()) {
			throw std::runtime_error("Complete signature not found.");
		}
		return it->second;
	}

	/**
 * Finalize an incomplete state.
 *
 * Ownership policy:
 *   - the state's incompleteSignature is consumed,
 *   - if the completed signature is new, the moved child signatures are adopted
 *     by newly created child states,
 *   - if the completed signature merges into an existing canonical state, the
 *     temporary finalized branch is discarded.
 */
	StateId finalize_state(StateId id) {
		check_state_id(id);

		AutomatonNode &node = states_[id];
		if (node.status != StateStatus::Incomplete || !node.incompleteSignature.has_value()) {
			throw std::runtime_error("finalize_state requires an incomplete state.");
		}

		auto finalized = std::move(*node.incompleteSignature).finalize_and_split();

		// LearnerAutomaton now takes responsibility for the ownership transition.
		node.output = finalized.output;
		node.completeSignature = finalized.completeSignature;
		node.incompleteSignature.reset();

		auto it = completeSignatureToStateId_.find(node.completeSignature);
		if (it != completeSignatureToStateId_.end()) {
			StateId canonicalId = it->second;

			node.status = StateStatus::Merged;
			node.mergedInto = canonicalId;

			if (node.parentId != invalidStateId) {
				states_[node.parentId].transitions[node.parentSymbol] = canonicalId;
			}

			return canonicalId;
		}

        node.status = StateStatus::Complete;
        node.mergedInto = invalidStateId;

        completeSignatureToStateId_.emplace(node.completeSignature, id);

        for (auto& child : finalized.children) {
            const Symbol a = child.symbol;

            if (a >= alphabetSize_) {
                throw std::runtime_error("Child symbol out of range in finalized signature.");
            }

            StateId childId = create_incomplete_state_impl(id, a);
            states_[childId].incompleteSignature = std::move(child.signature);
            node.transitions[a] = childId;
        }

        return id;
    }

	/**
	 * Move from a complete state through a transition symbol.
	 *
	 * @param from Source state id.
	 * @param a Input symbol.
	 * @return Target state id.
	 *
	 * @throws std::runtime_error If the source state is invalid, incomplete, merged, or the transition is not yet assigned.
	 */
	[[nodiscard]] StateId transition(StateId from, Symbol a) const {
		check_state_id(from);
		check_symbol(a);

		const AutomatonNode &node = states_[from];
		if (node.status != StateStatus::Complete) {
			throw std::runtime_error("Transitions are only valid from complete states.");
		}

		StateId target = node.transitions[a];
		if (target == invalidStateId) {
			throw std::runtime_error("Transition not assigned.");
		}

		return target;
	}

private:
	/**
	 * Create a new incomplete learner state.
	 *
	 * The created node owns an empty SignatureTree and has all transitions
	 * initialized to invalidStateId.
	 *
	 * @param parentId Identifier of the parent state, or invalidStateId for the root.
	 * @param parentSymbol Input symbol leading from the parent to this state.
	 * @return Newly created state id.
	 */
	StateId create_incomplete_state_impl(StateId parentId, Symbol parentSymbol) {
		StateId id = states_.size();

		AutomatonNode node;
		node.id = id;
		node.status = StateStatus::Incomplete;
		node.incompleteSignature.emplace(signatureDepth_, alphabetSize_);
		node.transitions.assign(alphabetSize_, invalidStateId);
		node.parentId = parentId;
		node.parentSymbol = parentSymbol;


		states_.push_back(std::move(node));
		return id;
	}

	/**
	 * Check whether a state id is valid.
	 *
	 * @throws std::runtime_error If the id is out of range.
	 */
	void check_state_id(StateId id) const {
		if (id >= states_.size()) {
			throw std::runtime_error("LearnerAutomaton state id out of range.");
		}
	}

	/**
	 * Check whether a symbol index is valid.
	 *
	 * @throws std::runtime_error If the symbol is out of range.
	 */
	void check_symbol(Symbol a) const {
		if (a >= alphabetSize_) {
			throw std::runtime_error("LearnerAutomaton symbol out of range.");
		}
	}
};
