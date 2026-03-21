#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class Automaton {
public:
    using State = std::size_t;
    using Symbol = std::size_t;
    using Output = std::string;

private:
    std::size_t input_count_{0};
    std::size_t state_count_{0};
    State initial_state_{0};

    // transitions[symbol][state] = next state
    std::vector<std::vector<State>> transitions_;
    std::vector<Output> outputs_;

public:
    Automaton() = default;

    Automaton(std::size_t input_count,
              std::size_t state_count,
              std::vector<std::vector<State>> transitions,
              std::vector<Output> outputs,
              State initial_state = 0)
        : input_count_(input_count),
          state_count_(state_count),
          initial_state_(initial_state),
          transitions_(std::move(transitions)),
          outputs_(std::move(outputs)) {
        validate();
    }

    [[nodiscard]] std::size_t input_count() const { return input_count_; }
    [[nodiscard]] std::size_t state_count() const { return state_count_; }
    [[nodiscard]] State initial_state() const { return initial_state_; }

    [[nodiscard]] const Output& output(State q) const {
        check_state(q);
        return outputs_[q];
    }

    [[nodiscard]] State next_state(Symbol a, State q) const {
        check_symbol(a);
        check_state(q);
        return transitions_[a][q];
    }

    [[nodiscard]] State next_state(State q, const std::vector<Symbol>& word) const {
        check_state(q);
        State current = q;
        for (Symbol a : word) {
            check_symbol(a);
            current = transitions_[a][current];
        }
        return current;
    }

    [[nodiscard]] const Output& output_at(State q, const std::vector<Symbol>& word) const {
        return output(next_state(q, word));
    }

    static Automaton from_string(const std::string& spec) {
        const auto colon_pos = spec.find(':');
        if (colon_pos == std::string::npos) {
            throw std::runtime_error("Missing ':' in automaton specification.");
        }

        const std::string left = spec.substr(0, colon_pos);
        const std::string right = spec.substr(colon_pos + 1);

        std::istringstream left_in(left);
        std::size_t input_count = 0;
        std::size_t state_count = 0;

        if (!(left_in >> input_count >> state_count)) {
            throw std::runtime_error("Failed to read input/state counts.");
        }

        if (input_count == 0 || state_count == 0) {
            throw std::runtime_error("Input/state counts must be positive.");
        }

        const std::size_t expected = input_count * state_count;
        std::vector<std::size_t> flat;
        flat.reserve(expected);

        std::size_t x = 0;
        while (left_in >> x) {
            flat.push_back(x);
        }

        if (flat.size() != expected) {
            throw std::runtime_error("Wrong number of transitions.");
        }

        std::vector<std::vector<State>> transitions(
            input_count, std::vector<State>(state_count));

        std::size_t k = 0;
        for (std::size_t a = 0; a < input_count; ++a) {
            for (std::size_t q = 0; q < state_count; ++q) {
                transitions[a][q] = flat[k++];
            }
        }

        std::vector<Output> outputs;
        {
            std::istringstream right_in(right);
            std::vector<std::string> tokens;
            std::string tok;
            while (right_in >> tok) {
                tokens.push_back(tok);
            }

            if (tokens.size() == state_count) {
                outputs = std::move(tokens);
            } else {
                std::string compact;
                for (char c : right) {
                    if (!std::isspace(static_cast<unsigned char>(c))) {
                        compact.push_back(c);
                    }
                }

                if (compact.size() != state_count) {
                    throw std::runtime_error("Wrong number of outputs.");
                }

                outputs.reserve(state_count);
                for (char c : compact) {
                    outputs.emplace_back(1, c);
                }
            }
        }

        return Automaton(input_count, state_count,
                         std::move(transitions), std::move(outputs), 0);
    }

    static std::vector<Automaton> read_all_from_file(const std::string& path) {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("Cannot open file: " + path);
        }

        std::vector<Automaton> result;
        std::string line;

        while (std::getline(in, line)) {
            trim_in_place(line);

            if (line.empty()) {
                continue;
            }

            if (line[0] == '#') {
                continue;
            }

            result.push_back(from_string(line));
        }

        return result;
    }

private:
    void validate() const {
        if (input_count_ == 0) {
            throw std::runtime_error("Automaton must have at least one input symbol.");
        }
        if (state_count_ == 0) {
            throw std::runtime_error("Automaton must have at least one state.");
        }
        if (initial_state_ >= state_count_) {
            throw std::runtime_error("Initial state out of range.");
        }
        if (transitions_.size() != input_count_) {
            throw std::runtime_error("Transition matrix has wrong number of symbol rows.");
        }
        for (const auto& row : transitions_) {
            if (row.size() != state_count_) {
                throw std::runtime_error("Transition row has wrong number of states.");
            }
            for (State q : row) {
                if (q >= state_count_) {
                    throw std::runtime_error("Transition target out of range.");
                }
            }
        }
        if (outputs_.size() != state_count_) {
            throw std::runtime_error("Wrong number of outputs.");
        }
    }

    void check_state(State q) const {
        if (q >= state_count_) {
            throw std::runtime_error("State out of range.");
        }
    }

    void check_symbol(Symbol a) const {
        if (a >= input_count_) {
            throw std::runtime_error("Input symbol out of range.");
        }
    }

    static void trim_in_place(std::string& s) {
        const auto first = s.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos) {
            s.clear();
            return;
        }
        const auto last = s.find_last_not_of(" \t\n\r\f\v");
        s = s.substr(first, last - first + 1);
    }
};