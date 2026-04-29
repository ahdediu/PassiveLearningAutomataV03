#pragma once

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <queue>
#include <set>
#include <unordered_set>
#include <map>
#include <optional>

#include "types.hpp"

class Automaton {
public:
public:
    using State = core_types::StateId;
    using Symbol = core_types::Symbol;
    using Output = core_types::Output;

private:
    std::size_t input_count_{0};
    std::size_t state_count_{0};
    State initial_state_{0};

    // transitions[symbol][state] = next state
    // Complete deterministic transition table:
    // transitions_[a][q] is the unique successor of state q under symbol a.
    // All transitions must be defined and must target a state in 0...state_count_-1.
    std::vector<std::vector<State> > transitions_;
    std::vector<Output> outputs_;
    std::string description_;
    std::map<State, std::string> state_descriptions_;

public:
    Automaton() = default;

    /**
     * Construct a deterministic complete Moore automaton.
     * @throws std::runtime_error If the automaton is malformed.
     */
    Automaton(std::size_t input_count,
              std::size_t state_count,
              std::vector<std::vector<State> > transitions = {},
              std::vector<Output> outputs = {},
              State initial_state = 0)
        : input_count_(input_count),
          state_count_(state_count),
          initial_state_(initial_state),
          transitions_(std::move(transitions)),
          outputs_(std::move(outputs)) {
        if (transitions_.empty() && input_count_ > 0 && state_count_ > 0) {
            transitions_.assign(input_count_, std::vector<State>(state_count_, 0));
        }
        if (outputs_.empty() && state_count_ > 0) {
            outputs_.assign(state_count_, "");
        }
        validate();
    }

    // Constructor to match the tools/generateIndoorMaps expectation
    // (state_count, input_count, optional_maps...)
    template<typename T1, typename T2>
    Automaton(std::size_t state_count, std::size_t input_count, T1, T2)
        : input_count_(input_count), state_count_(state_count), initial_state_(0) {
        transitions_.assign(input_count_, std::vector<State>(state_count_, 0));
        outputs_.assign(state_count_, "");
    }

    // Compatibility methods for generator tools
    [[nodiscard]] std::size_t getInputSymbolCount() const { return input_count_; }
    [[nodiscard]] std::size_t getStateCount() const { return state_count_; }
    [[nodiscard]] State getNextState(State q, Symbol a) const { return next_state(a, q); }
    [[nodiscard]] const Output& getOutput(State q) const { return output(q); }

    void setOutput(State q, Output o) {
        check_state(q);
        outputs_[q] = std::move(o);
    }

    void setOutput(State q, int o) {
        check_state(q);
        outputs_[q] = std::to_string(o);
    }

    void addTransition(Symbol a, State q, State next) {
        check_symbol(a);
        check_state(q);
        check_state(next);
        transitions_[a][q] = next;
    }

    void set_description(std::string d) { description_ = std::move(d); }
    [[nodiscard]] const std::string& get_description() const { return description_; }

    template<typename MapType>
    void set_state_description(MapType desc) {
        state_descriptions_.clear();
        for (auto const& [k, v] : desc) {
            state_descriptions_[static_cast<State>(k)] = v;
        }
    }

    [[nodiscard]] const std::map<State, std::string>& get_state_descriptions() const { return state_descriptions_; }

    [[nodiscard]] std::size_t input_count() const { return input_count_; }
    [[nodiscard]] std::size_t state_count() const { return state_count_; }
    [[nodiscard]] State initial_state() const { return initial_state_; }

    [[nodiscard]] const Output &output(State q) const {
        check_state(q);
        return outputs_[q];
    }

    [[nodiscard]] State next_state(Symbol a, State q) const {
        check_symbol(a);
        check_state(q);
        return transitions_[a][q];
    }

    [[nodiscard]] State next_state(State q, const std::vector<Symbol> &word) const {
        check_state(q);
        State current = q;
        for (Symbol a: word) {
            check_symbol(a);
            current = transitions_[a][current];
        }
        return current;
    }

    [[nodiscard]] const Output &output_at(State q, const std::vector<Symbol> &word) const {
        return output(next_state(q, word));
    }

    /**
     * Parse an automaton from a compact textual specification.
     *
     * Format:
     *   input_count state_count transitions... : outputs
     *
     * Transitions are listed row-wise by input symbol.
     * Outputs may be given either as whitespace-separated tokens
     * or in compact single-character form.
     *
     * @param spec String specification of the automaton.
     * @return Parsed automaton with initial state 0.
     *
     * @throws std::runtime_error If parsing fails or the specification is invalid.
     */
    static Automaton from_string(const std::string &spec) {
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

        std::vector<std::vector<State> > transitions(
            input_count, std::vector<State>(state_count));

        std::size_t k = 0;
        for (std::size_t a = 0; a < input_count; ++a) {
            for (std::size_t q = 0; q < state_count; ++q) {
                transitions[a][q] = flat[k++];
            }
        }

        std::vector<Output> outputs; {
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
                for (char c: right) {
                    if (!std::isspace(static_cast<unsigned char>(c))) {
                        compact.push_back(c);
                    }
                }

                if (compact.size() != state_count) {
                    throw std::runtime_error("Wrong number of outputs.");
                }

                outputs.reserve(state_count);
                for (char c: compact) {
                    outputs.emplace_back(1, c);
                }
            }
        }

        return {
            input_count, state_count,
            std::move(transitions), std::move(outputs), 0
        };
    }

    /**
     * Read all automata from a text file.
     *
     * Blank lines and lines beginning with '#' are ignored.
     * Each remaining line must contain one automaton specification.
     *
     * @param path Path to the input file.
     * @return Vector of parsed automata.
     *
     * @throws std::runtime_error If the file cannot be opened or a line is invalid.
     */
    static std::vector<Automaton> read_all_from_file(const std::string &path) {
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

    /**
 * Compute the set of states reachable from the initial state.
 * Uses BFS over the transition graph.
 * @return Set of reachable states.
 */
    [[nodiscard]] std::set<State> reachable_states() const {
        std::set<State> result;
        std::queue<State> q; //states to be processed
        std::unordered_set<State> visited;

        q.push(initial_state_);
        visited.insert(initial_state_);

        while (!q.empty()) {
            State current = q.front();
            q.pop();
            result.insert(current);

            for (Symbol a = 0; a < input_count_; ++a) {
                State next = transitions_[a][current];
                if (!visited.contains(next)) {
                    visited.insert(next);
                    q.push(next);
                }
            }
        }

        return result;
    }

    /**
 * Compute the initial partition of states based on outputs.
 * States are grouped into equivalence classes where states
 * with identical outputs are placed in the same class.
 * Unreachable states are assigned class -1.
 * @return Vector mapping each state to its partition class id.
 *         Unreachable states are assigned -1.
 */
    [[nodiscard]] std::vector<int> partition_by_output() const {
        std::vector<int> part(state_count_, -1);
        const auto reachable = reachable_states();

        std::map<Output, int> class_of_output;
        int next_class = 0;

        for (State q: reachable) {
            const auto &out = outputs_[q];
            auto it = class_of_output.find(out);
            if (it == class_of_output.end()) {
                class_of_output[out] = next_class;
                part[q] = next_class;
                ++next_class;
            } else {
                part[q] = it->second;
            }
        }

        return part;
    }

    /**
 * Refine a partition using successor equivalence.
 *
 * Two states remain in the same class iff:
 *   - they have the same output, and
 *   - their successors under each input symbol belong to the same classes.
 *
 * This corresponds to one step of partition refinement.
 *
 * @param prev Previous partition.
 * @return Refined partition.
 *
 * @throws std::runtime_error If partition size is invalid.
 */
    [[nodiscard]] std::vector<int> refine_partition(const std::vector<int> &prev) const {
        if (prev.size() != state_count_) {
            throw std::runtime_error("Partition size does not match number of states.");
        } // state q-> class
        // Key idea: states are equivalent if they produce the same output
        // and transition to equivalent states under all inputs.
        std::vector<int> part(state_count_, -1);
        const auto reachable = reachable_states();

        using Key = std::pair<Output, std::vector<int> >;
        std::map<Key, int> class_of_key;
        int next_class = 0;

        for (State q: reachable) {
            std::vector<int> succ_classes;
            succ_classes.reserve(input_count_);

            for (Symbol a = 0; a < input_count_; ++a) {
                State nxt = transitions_[a][q];
                succ_classes.push_back(prev[nxt]);
            }

            Key key{outputs_[q], std::move(succ_classes)};

            auto it = class_of_key.find(key);
            if (it == class_of_key.end()) {
                class_of_key[key] = next_class;
                part[q] = next_class;
                ++next_class;
            } else {
                part[q] = it->second;
            }
        }

        return part;
    }

    /**
 * Compute the distinguishability degree via partition refinement.
 *
 * The degree is the number of refinement steps required until
 * the partition stabilizes.
 *
 * @return Distinguishability degree.
 */
    [[nodiscard]] int distinguishability_degree_by_partition() const {
        auto part = partition_by_output();
        int depth = 0;

        while (true) {
            auto next = refine_partition(part);
            if (next == part) {
                return depth;
            }
            part = std::move(next);
            ++depth;
        }
    }

    /**
     * Check whether two states have different outputs.
     *
     * @param p First state.
     * @param q Second state.
     * @return True iff the outputs of p and q differ.
     *
     * @throws std::runtime_error If either state is out of range.
     */
    [[nodiscard]] bool outputs_differ(State p, State q) const {
        check_state(p);
        check_state(q);
        return outputs_[p] != outputs_[q];
    }

    /**
     * Compute the shortest word distinguishing two states.
     *
     * A word w distinguishes states p and q if:
     *   output(δ(p,w)) ≠ output(δ(q,w)).
     *
     * Uses BFS over pairs of states (product construction).
     *
     * @param p First state.
     * @param q Second state.
     * @return Shortest distinguishing word, or std::nullopt if none exists.
     *
     * @note Returns empty word if outputs(p) ≠ outputs(q).
     */
    [[nodiscard]] std::optional<std::vector<Symbol> > shortest_separating_word(State p, State q) const {
        check_state(p);
        check_state(q);

        if (p == q) {
            return std::nullopt;
        }

        // Empty word already separates them.
        if (outputs_[p] != outputs_[q]) {
            return std::vector<Symbol>{};
        }

        struct Node {
            State p;
            State q;
            std::vector<Symbol> word;
        };

        auto normalize_pair = [](State a, State b) -> std::pair<State, State> {
            return (a < b)
                       ? std::pair<State, State>{a, b}
                       : std::pair<State, State>{b, a};
        };
        // BFS over unordered pairs of states (product automaton).
        // Nodes: (p,q)
        // Edge: (p,q) --a--> (δ(p,a), δ(q,a))
        // Goal: reach a pair with different outputs.
        std::queue<Node> bfs;
        std::vector<std::vector<bool> > visited(
            state_count_, std::vector<bool>(state_count_, false));

        auto [p0, q0] = normalize_pair(p, q);
        visited[p0][q0] = true;
        bfs.push(Node{p, q, {}});

        while (!bfs.empty()) {
            Node cur = std::move(bfs.front());
            bfs.pop();

            for (Symbol a = 0; a < input_count_; ++a) {
                State p_next = transitions_[a][cur.p];
                State q_next = transitions_[a][cur.q];

                std::vector<Symbol> next_word = cur.word;
                next_word.push_back(a);

                if (outputs_[p_next] != outputs_[q_next]) {
                    return next_word;
                }

                if (p_next == q_next) {
                    continue;
                }

                auto [u, v] = normalize_pair(p_next, q_next);
                if (!visited[u][v]) {
                    visited[u][v] = true;
                    bfs.push(Node{p_next, q_next, std::move(next_word)});
                }
            }
        }

        return std::nullopt;
    }

    /**
     * Compute the length of the shortest distinguishing word between two states.
     *
     * @param p First state.
     * @param q Second state.
     * @return Length of shortest distinguishing word, or std::nullopt.
     */
    [[nodiscard]] std::optional<int> shortest_separating_length(State p, State q) const {
        auto word = shortest_separating_word(p, q);
        if (!word.has_value()) {
            return std::nullopt;
        }
        return static_cast<int>(word->size());
    }

    /**
     * Compute the distinguishability degree using BFS.
     *
     * The degree is the maximum shortest distinguishing length over all
     * unordered pairs of reachable states.
     *
     * @return Distinguishability degree.
     */
    [[nodiscard]] int distinguishability_degree_by_bfs() const {
        const auto reachable = reachable_states();
        std::vector<State> states(reachable.begin(), reachable.end());

        int best = 0;

        for (std::size_t i = 0; i < states.size(); ++i) {
            for (std::size_t j = i + 1; j < states.size(); ++j) {
                auto len = shortest_separating_length(states[i], states[j]);
                if (!len.has_value()) {
                    continue;
                }
                if (*len > best) {
                    best = *len;
                }
            }
        }

        return best;
    }

private:
    /**
 * Validate internal consistency of the automaton.
 *
 * Checks:
 *   - positive number of states and inputs
 *   - valid initial state
 *   - complete transition table
 *   - transitions within bounds
 *   - correct number of outputs
 *
 * @throws std::runtime_error If validation fails.
 */
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
        for (const auto &row: transitions_) {
            if (row.size() != state_count_) {
                throw std::runtime_error("Transition row has wrong number of states.");
            }
            for (State q: row) {
                if (q >= state_count_) {
                    throw std::runtime_error("Transition target out of range.");
                }
            }
        }
        if (outputs_.size() != state_count_) {
            throw std::runtime_error("Wrong number of outputs.");
        }
    }

    /**
     * Ensure state index is valid.
     *
     * @throws std::runtime_error If out of range.
     */
    void check_state(State q) const {
        if (q >= state_count_) {
            throw std::runtime_error("State out of range.");
        }
    }

    /**
     * Ensure input symbol index is valid.
     *
     * @throws std::runtime_error If out of range.
     */
    void check_symbol(Symbol a) const {
        if (a >= input_count_) {
            throw std::runtime_error("Input symbol out of range.");
        }
    }

    /**
         * Remove leading and trailing whitespace from a string in place.
         *
         * If the string contains only whitespace, it becomes empty.
         */
    static void trim_in_place(std::string &s) {
        const auto first = s.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos) {
            s.clear();
            return;
        }
        const auto last = s.find_last_not_of(" \t\n\r\f\v");
        s = s.substr(first, last - first + 1);
    }
};
