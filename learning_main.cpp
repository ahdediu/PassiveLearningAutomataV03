#include <fstream>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <random>
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

struct RunResult {
    std::string name;
    std::string protocol;
    LearningStatistics stats;
    double duration_ms;
    bool equivalent;
    std::size_t s;  // Complete states
    std::size_t m;  // Merged states
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
        protocol->reset();
        while (!learner->stopCondition()) {
            protocol->step();
        }
    } catch (const std::exception& e) {
        std::cerr << "\n  Protocol error: " << e.what() << std::endl;
    }

    auto end_time = std::chrono::high_resolution_clock::now();

    std::cout << "\r" << std::string(80, ' ') << "\r"
              << std::flush;  // Clear progress line

    double duration =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::size_t complete_count = 0;
    for (std::size_t i = 0; i < model_ptr->state_count(); ++i) {
        if (model_ptr->is_complete(i)) {
            complete_count++;
        }
    }
    std::size_t total_states = model_ptr->state_count();

    return {name,
            protocol_name,
            protocol->statistics(),
            duration,
            check_equivalence(target, *learner, *model_ptr),
            complete_count,
            total_states - complete_count,
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
            total_duration += res.duration_ms;
            k = res.k;
            if (res.equivalent) {
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
            total_duration / num_runs,
            (double)successful_runs / num_runs * 100.0,
            k,
            d};
}

void print_comparison(const std::vector<AggregatedResult>& results) {
    std::cout << "\n" << std::setfill('=') << std::setw(145) << "" << std::setfill(' ') << "\n";
    std::cout << std::left << std::setw(20) << "Example" 
              << std::setw(10) << "Protocol" 
              << std::setw(5) << "d"
              << std::right << std::setw(12) << "Trials(T)" 
              << std::setw(10) << "?" 
              << std::setw(10) << "!" 
              << std::setw(10) << "Backs"
              << std::setw(10) << "s (Cpl)" 
              << std::setw(10) << "m (Mrg)"
              << std::setw(15) << "s+m = kn+1" 
              << std::setw(12) << "Time(ms)" 
              << std::setw(12) << "Success %" << "\n";
    std::cout << std::setfill('-') << std::setw(145) << "" << std::setfill(' ') << "\n";

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
                  << std::setw(15) << (check + " (" + std::to_string((int)left) + ")") 
                  << std::setw(12) << res.avg_duration_ms 
                  << std::setw(12) << res.success_rate << "%\n";
    }
    std::cout << std::setfill('=') << std::setw(145) << "" << std::setfill(' ') << "\n\n";
}

int main() {
    try {
        std::vector<AggregatedResult> results;
        const int num_runs = 10;

        const bool repeatable_experiments = false;

        auto run_benchmarks = [&](const Automaton& target, const std::string& name, bool skip_no_loop_detect = false, int depth = -1) {
            if (!skip_no_loop_detect) {
                //results.push_back(run_benchmark(target, name, ProtocolType::Reset, num_runs, depth, repeatable_experiments));
            }
            results.push_back(run_benchmark(target, name, ProtocolType::Back, num_runs, depth, repeatable_experiments));
        };

//        run_benchmarks(examples::two_state_flip(), "two_state_flip");
//        run_benchmarks(examples::three_state_cycle(), "three_state_cycle");
//        run_benchmarks(examples::degree_one_example(), "degree_one_example", true);
//        run_benchmarks(examples::degree_two_example(), "degree_two_example", true);
//        run_benchmarks(examples::reversible_maze_example(), "reversible_maze");
//        run_benchmarks(examples::parser_symbolic_example(), "parser_symbolic");
//        run_benchmarks(examples::parser_numeric_example(), "parser_numeric");

        struct BenchmarkFile {
            std::string relative_path;
            std::string prefix;
        };

        std::vector<BenchmarkFile> benchmark_files = {
            //{"data/random/D1testAutomata10to640expStates.txt", "D1_"},
            {"data/grid1.txt", "G1_"},
            {"data/grid2.txt", "G2_"},
            {"data/grid3.txt", "G3_"},
            {"data/grid4.txt", "G4_"}
        };

        for (const auto& bf : benchmark_files) {
            std::vector<std::string> potential_paths = {
                bf.relative_path,
                "../" + bf.relative_path,
                "../../" + bf.relative_path,
                "../../../" + bf.relative_path
            };
            
            std::string file_path;
            for (const auto& path : potential_paths) {
                std::ifstream f(path);
                if (f.good()) {
                    file_path = path;
                    break;
                }
            }

            if (file_path.empty()) {
                continue; // Skip if not found
            }

            try {
                auto file_automata = Automaton::read_all_from_file(file_path);
                for (const auto& target : file_automata) {
                    std::string name = bf.prefix + std::to_string(target.state_count());
                    run_benchmarks(target, name);
                }
            } catch (const std::exception& e) {
                std::cerr << "Error loading " << file_path << ": " << e.what() << std::endl;
            }
        }

        print_comparison(results);

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
