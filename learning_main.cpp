#include <cmath>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include "core/automaton.hpp"
#include "examples/automata_examples.hpp"
#include "reset.hpp"
#include "back.hpp"

/**
 * Check whether a target Automaton and a Learner's current complete model are equivalent.
 */
bool check_equivalence(const Automaton& target,
                       const Learner& learner,
                       const LearnerAutomaton& learner_model) {
    if (learner.incomplete_state_count() > 0) {
        return false;
    }

    std::size_t learner_complete_count = 0;
    for (std::size_t i = 0; i < learner_model.state_count(); ++i) {
        if (learner_model.is_complete(i)) {
            ++learner_complete_count;
        }
    }

    std::map<Automaton::State, LearnerAutomaton::StateId> target_to_learner;
    std::queue<Automaton::State> work;

    const Automaton::State target_init = target.initial_state();
    const LearnerAutomaton::StateId learner_init = learner_model.initial_state();

    if (target.output(target_init) != learner_model.state(learner_init).output) {
        return false;
    }

    target_to_learner[target_init] = learner_init;
    work.push(target_init);

    while (!work.empty()) {
        const auto q_t = work.front();
        work.pop();
        const auto q_l = target_to_learner[q_t];

        for (std::size_t a = 0; a < target.input_count(); ++a) {
            const auto next_t = target.next_state(a, q_t);
            const auto next_l = learner_model.transition(q_l, a);

            // In a complete model, all transitions must be defined
            if (next_l == LearnerAutomaton::invalidStateId) {
                return false;
            }

            if (target.output(next_t) != learner_model.state(next_l).output) {
                return false;
            }

            auto it = target_to_learner.find(next_t);
            if (it == target_to_learner.end()) {
                target_to_learner[next_t] = next_l;
                work.push(next_t);
            } else if (it->second != next_l) {
                // This would mean the same target state maps to two different learner states,
                // which is impossible in a deterministic mapping.
                return false;
            }
        }
    }

    // Check surjectivity: all complete learner states must be reached by the mapping.
    // This ensures the learner doesn't have extra behavior or unreachable states.
    std::set<LearnerAutomaton::StateId> reached_learner_states;
    for (const auto& pair : target_to_learner) {
        reached_learner_states.insert(pair.second);
    }

    return reached_learner_states.size() == learner_complete_count;
}

enum class ProtocolType {
    Reset,
    Back
};

enum class ExperimentGroup {
    SmallExamples,
    Table1,
    Table2,
    AllINS
};

enum class SeedMode {
    Deterministic,
    RandomDevice
};

// Source-level experiment selection for CLion runs.
constexpr ExperimentGroup selected_experiment_group = ExperimentGroup::AllINS;
constexpr SeedMode selected_seed_mode = SeedMode::Deterministic;
constexpr int manuscript_num_runs = 10;
constexpr int small_example_num_runs = 1;

struct RunResult {
    std::string name;
    std::string protocol;
    LearningStatistics stats;
    double duration_ms;
    bool equivalent;
    std::size_t s;  // Complete states
    std::size_t m;  // Merged states
    std::size_t incomplete;
    std::size_t generated;
    std::size_t k;  // Alphabet size
    int d;          // Distinguishability degree
};

struct AggregatedResult {
    std::string name;
    std::string protocol;
    double avg_trials;
    double avg_queries;
    double avg_back_signals;
    double avg_total_backs;
    double avg_s;
    double avg_m;
    double avg_incomplete;
    double avg_generated;
    double avg_duration_ms;
    double success_rate;
    std::size_t k;
    int d;          // Distinguishability degree
};

RunResult run_example(const Automaton& target,
                      const std::string& name,
                      ProtocolType type,
                      int forced_depth = -1,
                      unsigned int seed = 42) {
    int signature_depth = target.distinguishability_degree_by_partition();
    if (forced_depth != -1) {
        signature_depth = forced_depth;
    }

    std::unique_ptr<Teacher> teacher_ptr;
    std::unique_ptr<Learner> learner;
    std::unique_ptr<LearningProtocol> protocol;
    LearnerAutomaton* model_ptr = nullptr;
    std::string protocol_name;

    switch (type) {
        case ProtocolType::Reset: {
            auto r_teacher = std::make_unique<ResetTeacher>(target, seed);
            auto r_learner =
                std::make_unique<ResetLearner>(target.input_count(), signature_depth);
            model_ptr = &r_learner->automaton();
            protocol = std::make_unique<ResetProtocol>(*r_teacher, *r_learner);
            teacher_ptr = std::move(r_teacher);
            learner = std::move(r_learner);
            protocol_name = "Reset";
            break;
        }
        case ProtocolType::Back: {
            auto b_teacher = std::make_unique<BackTeacher>(target, seed);
            auto b_learner =
                std::make_unique<BackLearner>(target.input_count(), signature_depth);
            model_ptr = &b_learner->automaton();

            BackTeacher& b_teacher_ref = *b_teacher;
            BackLearner& b_learner_ref = *b_learner;

            protocol = std::make_unique<BackProtocol>(b_teacher_ref, b_learner_ref);

            teacher_ptr = std::move(b_teacher);
            learner = std::move(b_learner);
            protocol_name = "Back";
            break;
        }
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Set progress callback
    protocol->set_progress_callback([](const LearningStatistics& s) {
        if (s.trials % 1000 == 0) {  // Throttle updates more for performance
            std::cout << "\r  Progress: T=" << std::setw(9) << s.trials
                      << " ?=" << std::setw(4) << s.queries << " !=" << std::setw(4)
                      << s.backs << " s=" << std::setw(4) << s.complete_states
                      << " i=" << std::setw(4) << s.incomplete_states << " m="
                      << std::setw(4) << s.merges << "    " << std::flush;
        }
    });

    try {
        protocol->run();
    } catch (const std::exception& e) {
        std::cerr << "\n  Protocol error: " << e.what() << std::endl;
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    std::cout << "\r" << std::string(80, ' ') << "\r"
              << std::flush;  // Clear progress line

    double duration =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::size_t complete_count = 0;
    std::size_t merged_count = 0;
    std::size_t incomplete_count = 0;
    for (std::size_t i = 0; i < model_ptr->state_count(); ++i) {
        switch (model_ptr->state(i).status) {
            case LearnerAutomaton::StateStatus::Complete:
                ++complete_count;
                break;
            case LearnerAutomaton::StateStatus::Merged:
                ++merged_count;
                break;
            case LearnerAutomaton::StateStatus::Incomplete:
                ++incomplete_count;
                break;
        }
    }
    std::size_t total_states = model_ptr->state_count();

    return {name,
            protocol_name,
            protocol->statistics(),
            duration,
            check_equivalence(target, *learner, *model_ptr),
            complete_count,
            merged_count,
            incomplete_count,
            total_states,
            target.input_count(),
            signature_depth};
}

AggregatedResult run_benchmark(const Automaton& target,
                                const std::string& name,
                                ProtocolType type,
                                int num_runs = 10,
                                int forced_depth = -1,
                                bool repeatable_experiments = true) {
    std::string protocol_str;
    switch (type) {
        case ProtocolType::Reset:
            protocol_str = "Reset";
            break;
        case ProtocolType::Back:
            protocol_str = "Back";
            break;
    }

    std::cout << "Benchmarking " << name << " (" << protocol_str << ") over " << num_runs
              << " runs..." << (repeatable_experiments ? " [REPEATABLE]" : " [RANDOM]")
              << (forced_depth != -1
                      ? " [FORCED DEPTH " + std::to_string(forced_depth) + "]"
                      : "")
              << std::endl;

    double total_trials = 0;
    double total_queries = 0;
    double total_back_signals = 0;
    double total_backs = 0;
    double total_s = 0;
    double total_m = 0;
    double total_incomplete = 0;
    double total_generated = 0;
    double total_duration = 0;
    int successful_runs = 0;
    std::size_t k = 0;

    std::random_device random_device;

    for (int i = 0; i < num_runs; ++i) {
        try {
            const unsigned int seed = repeatable_experiments
                                          ? static_cast<unsigned int>(42 + i)
                                          : random_device();

            RunResult res = run_example(target, name, type, forced_depth, seed);
            total_trials += res.stats.trials;
            total_queries += res.stats.queries;
            total_back_signals += res.stats.backs;
            total_backs += res.stats.queries + res.stats.backs;
            total_s += res.s;
            total_m += res.m;
            total_incomplete += res.incomplete;
            total_generated += res.generated;
            total_duration += res.duration_ms;
            k = res.k;
            if (res.equivalent && res.incomplete == 0) {
                successful_runs++;
            }
        } catch (const std::exception& e) {
            std::cerr << "\nError during run " << i << ": " << e.what() << std::endl;
        }
    }

    int d = target.distinguishability_degree_by_partition();
    if (forced_depth != -1) {
        d = forced_depth;
    }

    return {name,
            protocol_str,
            total_trials / num_runs,
            total_queries / num_runs,
            total_back_signals / num_runs,
            total_backs / num_runs,
            total_s / num_runs,
            total_m / num_runs,
            total_incomplete / num_runs,
            total_generated / num_runs,
            total_duration / num_runs,
            (double)successful_runs / num_runs * 100.0,
            k,
            d};
}

void print_comparison(const std::vector<AggregatedResult>& results) {
    constexpr int table_width = 174;
    std::cout << "\n" << std::setfill('=') << std::setw(table_width) << ""
              << std::setfill(' ') << "\n";
    std::cout << std::left << std::setw(20) << "Example" 
              << std::setw(10) << "Protocol" 
              << std::setw(5) << "d"
              << std::right << std::setw(12) << "Trials(T)" 
              << std::setw(10) << "?" 
              << std::setw(10) << "!" 
              << std::setw(10) << "? + !"
              << std::setw(10) << "s (Cpl)" 
              << std::setw(10) << "m (Mrg)"
              << std::setw(10) << "i (Rem)"
              << std::setw(12) << "Generated"
              << std::setw(15) << "s+m = kn+1" 
              << std::setw(12) << "Time(ms)" 
              << std::setw(12) << "Success %" << "\n";
    std::cout << std::setfill('-') << std::setw(table_width) << ""
              << std::setfill(' ') << "\n";

    for (const auto& res : results) {
        // Formula: s + m = k*n + 1 where n = s
        double left = res.avg_s + res.avg_m;
        double right = (double)res.k * res.avg_s + 1.0;
        std::string check = (std::abs(left - right) < 0.001) ? "OK" : "FAIL";

        std::cout << std::left << std::setw(20) << res.name 
                  << std::setw(10) << res.protocol 
                  << std::left << std::setw(5) << res.d
                  << std::right << std::setw(12) << std::fixed << std::setprecision(1) << res.avg_trials 
                  << std::setw(10) << (int)res.avg_queries 
                  << std::setw(10) << (int)res.avg_back_signals
                  << std::setw(10) << (int)res.avg_total_backs
                  << std::setw(10) << (int)res.avg_s 
                  << std::setw(10) << (int)res.avg_m
                  << std::setw(10) << (int)res.avg_incomplete
                  << std::setw(12) << (int)res.avg_generated
                  << std::setw(15) << (check + " (" + std::to_string((int)left) + ")") 
                  << std::setw(12) << res.avg_duration_ms 
                  << std::setw(12) << res.success_rate << "%\n";
    }
    std::cout << std::setfill('=') << std::setw(table_width) << ""
              << std::setfill(' ') << "\n\n";
}

struct DatasetSpec {
    std::string name;
    std::string relative_path;
    std::size_t expected_states;
};

std::filesystem::path resolve_required_dataset(const std::string& relative_path) {
    const std::filesystem::path requested(relative_path);
    std::vector<std::filesystem::path> roots;

    auto current = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        roots.push_back(current);
        if (!current.has_parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    roots.push_back(std::filesystem::path(__FILE__).parent_path());

    for (const auto& root : roots) {
        const auto candidate = root / requested;
        if (std::filesystem::is_regular_file(candidate)) {
            const auto resolved = std::filesystem::canonical(candidate);
            std::cout << "Resolved dataset: " << resolved << '\n';
            return resolved;
        }
    }

    throw std::runtime_error("Required dataset not found: " + relative_path);
}

Automaton load_required_dataset(const DatasetSpec& spec) {
    const auto path = resolve_required_dataset(spec.relative_path);
    auto automata = Automaton::read_all_from_file(path.string());
    if (automata.size() != 1) {
        throw std::runtime_error(spec.name + " must contain exactly one automaton: " +
                                 path.string());
    }
    if (automata.front().state_count() != spec.expected_states) {
        throw std::runtime_error(spec.name + " has " +
                                 std::to_string(automata.front().state_count()) +
                                 " states; expected " +
                                 std::to_string(spec.expected_states));
    }
    return std::move(automata.front());
}

int main() {
    try {
        std::vector<AggregatedResult> results;
        const bool repeatable_experiments =
            selected_seed_mode == SeedMode::Deterministic;

        auto run_protocols = [&](const Automaton& target,
                                 const std::string& name,
                                 int num_runs,
                                 bool run_reset,
                                 bool run_back) {
            if (run_reset) {
                results.push_back(run_benchmark(target, name, ProtocolType::Reset,
                                                num_runs, -1,
                                                repeatable_experiments));
            }
            if (run_back) {
                results.push_back(run_benchmark(target, name, ProtocolType::Back,
                                                num_runs, -1,
                                                repeatable_experiments));
            }
        };

        const std::vector<DatasetSpec> labeled_walls = {
            {"G1_176", "data/results/GridsWithDistinctWalls/grid1.txt", 176},
            {"G2_297", "data/results/GridsWithDistinctWalls/grid2.txt", 297},
            {"G3_176", "data/results/GridsWithDistinctWalls/grid3.txt", 176},
            {"G4_297", "data/results/GridsWithDistinctWalls/grid4.txt", 297},
        };
        const std::vector<DatasetSpec> no_wall_labels = {
            {"G5_86", "data/results/NoWallsNoLabelingHelp/grid1.txt", 86},
            {"G6_191", "data/results/NoWallsNoLabelingHelp/grid2.txt", 191},
            {"G7_94", "data/results/NoWallsNoLabelingHelp/grid3.txt", 94},
            {"G8_191", "data/results/NoWallsNoLabelingHelp/grid4.txt", 191},
        };
        const std::vector<DatasetSpec> wall_context_labels = {
            {"G09_86", "data/results/NoWallsLabelinHelp/grid1.txt", 86},
            {"G10_191", "data/results/NoWallsLabelinHelp/grid2.txt", 191},
            {"G11_94", "data/results/NoWallsLabelinHelp/grid3.txt", 94},
            {"G12_191", "data/results/NoWallsLabelinHelp/grid4.txt", 191},
        };

        auto run_small_examples = [&]() {
            run_protocols(examples::two_state_flip(), "two_state_flip",
                          small_example_num_runs, true, true);
            run_protocols(examples::degree_one_example(), "degree_one_example",
                          small_example_num_runs, true, true);
            run_protocols(examples::degree_two_example(), "degree_two_example",
                          small_example_num_runs, true, true);
        };

        auto run_table1 = [&]() {
            const auto d1_path = resolve_required_dataset(
                "data/random/D1testAutomata10to640expStates.txt");
            auto d1_automata = Automaton::read_all_from_file(d1_path.string());
            const std::vector<std::size_t> expected_sizes =
                {10, 20, 40, 80, 160, 320, 640};
            if (d1_automata.size() != expected_sizes.size()) {
                throw std::runtime_error("D1 archive must contain exactly seven automata");
            }
            for (std::size_t i = 0; i < expected_sizes.size(); ++i) {
                if (d1_automata[i].state_count() != expected_sizes[i]) {
                    throw std::runtime_error(
                        "D1 archive entry " + std::to_string(i) + " has " +
                        std::to_string(d1_automata[i].state_count()) +
                        " states; expected " + std::to_string(expected_sizes[i]));
                }
                run_protocols(d1_automata[i],
                              "D1_" + std::to_string(expected_sizes[i]),
                              manuscript_num_runs, true, true);
            }
            for (const auto& spec : labeled_walls) {
                run_protocols(load_required_dataset(spec), spec.name,
                              manuscript_num_runs, true, true);
            }
        };

        auto run_table2 = [&](bool include_labeled_walls) {
            if (include_labeled_walls) {
                for (const auto& spec : labeled_walls) {
                    run_protocols(load_required_dataset(spec), spec.name,
                                  manuscript_num_runs, false, true);
                }
            }
            for (const auto& spec : no_wall_labels) {
                run_protocols(load_required_dataset(spec), spec.name,
                              manuscript_num_runs, false, true);
            }
            for (const auto& spec : wall_context_labels) {
                run_protocols(load_required_dataset(spec), spec.name,
                              manuscript_num_runs, false, true);
            }
        };

        switch (selected_experiment_group) {
            case ExperimentGroup::SmallExamples:
                run_small_examples();
                break;
            case ExperimentGroup::Table1:
                run_table1();
                break;
            case ExperimentGroup::Table2:
                run_table2(true);
                break;
            case ExperimentGroup::AllINS:
                run_table1();
                // Table 1 already produces the four labeled-wall BACK rows.
                run_table2(false);
                break;
        }

        print_comparison(results);

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
