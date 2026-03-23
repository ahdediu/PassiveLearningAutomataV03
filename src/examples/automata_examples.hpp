//
// Created by Adrian Dediu on 23/03/2026.
//

#pragma once

#include <string>
#include <vector>

#include "core/automaton.hpp"

namespace examples {

	inline Automaton two_state_flip() {
		std::vector<std::vector<Automaton::State>> transitions = {
			{1, 0},
			{0, 1}
		};
		std::vector<Automaton::Output> outputs = {"+", "-"};
		return Automaton(2, 2, transitions, outputs, 0);
	}

	inline Automaton three_state_cycle() {
		std::vector<std::vector<Automaton::State>> transitions = {
			{1, 2, 0},
			{2, 0, 1}
		};
		std::vector<Automaton::Output> outputs = {"A", "B", "C"};
		return Automaton(2, 3, transitions, outputs, 0);
	}

	inline Automaton parser_symbolic_example() {
		return Automaton::from_string("2 5 3 3 0 2 0 4 0 0 1 2:+-+-+");
	}

	inline Automaton parser_numeric_example() {
		return Automaton::from_string("2 3 1 2 0 2 1 0:5 7 9");
	}

	inline Automaton degree_one_example() {
		std::vector<std::vector<Automaton::State>> transitions = {
			{0, 2, 2, 3},
			{1, 3, 2, 3}
		};
		std::vector<Automaton::Output> outputs = {"A", "A", "B", "C"};
		return Automaton(2, 4, transitions, outputs, 0);
	}

	inline Automaton unreachable_example() {
		std::vector<std::vector<Automaton::State>> transitions = {
			{1, 1, 3, 3},
			{1, 1, 3, 3}
		};
		std::vector<Automaton::Output> outputs = {"A", "B", "C", "D"};
		return Automaton(2, 4, transitions, outputs, 0);
	}

	inline Automaton degree_two_example() {
		std::vector<std::vector<Automaton::State>> transitions = {
			{2, 3, 4, 5, 4, 5, 0},
			{3, 2, 4, 5, 4, 5, 1}
		};
		std::vector<Automaton::Output> outputs = {"A", "A", "B", "B", "C", "D", "R"};
		return Automaton(2, 7, transitions, outputs, 6);
	}

	namespace specs {
		inline const std::string parser_symbolic =
			"2 5 3 3 0 2 0 4 0 0 1 2:+-+-+";

		inline const std::string parser_numeric =
			"2 3 1 2 0 2 1 0:5 7 9";
	}

} // namespace examples
