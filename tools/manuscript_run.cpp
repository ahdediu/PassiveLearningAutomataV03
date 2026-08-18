#include <chrono>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "back.hpp"
#include "core/automaton.hpp"
#include "core/learnerAutomaton.hpp"
#include "core/learningProtocol.hpp"
#include "reset.hpp"

namespace {

namespace fs = std::filesystem;

constexpr unsigned int first_seed = 42;
constexpr unsigned int last_seed = 51;

enum class ProtocolType { Reset, Back };
enum class ExperimentGroup { Table1, Table2, All };

struct Dataset {
    std::string name;
    Automaton automaton;
};

struct RunResult {
    std::string target;
    std::string protocol;
    unsigned int seed{};
    int depth{};
    std::size_t trials{};
    std::size_t queries{};
    std::size_t backs{};
    std::size_t complete{};
    std::size_t merged{};
    std::size_t incomplete{};
    std::size_t generated{};
    bool completed{};
    bool equivalent{};
    double time_ms{};
};

const fs::path project_root = PLA_PROJECT_SOURCE_DIR;

bool check_equivalence(const Automaton& target,
                       const Learner& learner,
                       const LearnerAutomaton& model) {
    if (learner.incomplete_state_count() != 0) {
        return false;
    }

    std::size_t complete_count = 0;
    for (std::size_t i = 0; i < model.state_count(); ++i) {
        if (model.is_complete(i)) {
            ++complete_count;
        }
    }

    std::map<Automaton::State, LearnerAutomaton::StateId> mapping;
    std::queue<Automaton::State> work;
    const auto target_initial = target.initial_state();
    const auto learner_initial = model.initial_state();

    if (target.output(target_initial) != model.state(learner_initial).output) {
        return false;
    }

    mapping[target_initial] = learner_initial;
    work.push(target_initial);

    while (!work.empty()) {
        const auto target_state = work.front();
        work.pop();
        const auto learner_state = mapping[target_state];

        for (std::size_t a = 0; a < target.input_count(); ++a) {
            const auto next_target = target.next_state(a, target_state);
            const auto next_learner = model.transition(learner_state, a);
            if (next_learner == LearnerAutomaton::invalidStateId ||
                target.output(next_target) != model.state(next_learner).output) {
                return false;
            }

            const auto [it, inserted] = mapping.emplace(next_target, next_learner);
            if (inserted) {
                work.push(next_target);
            } else if (it->second != next_learner) {
                return false;
            }
        }
    }

    std::set<LearnerAutomaton::StateId> reached_learner_states;
    for (const auto& [target_state, learner_state] : mapping) {
        static_cast<void>(target_state);
        reached_learner_states.insert(learner_state);
    }
    return reached_learner_states.size() == complete_count;
}

void count_states(const LearnerAutomaton& model,
                  std::size_t& complete,
                  std::size_t& merged,
                  std::size_t& incomplete) {
    complete = merged = incomplete = 0;
    for (std::size_t i = 0; i < model.state_count(); ++i) {
        if (model.is_complete(i)) {
            ++complete;
        } else if (model.is_merged(i)) {
            ++merged;
        } else {
            ++incomplete;
        }
    }
}

RunResult run_once(const Dataset& dataset, ProtocolType type, unsigned int seed) {
    const int depth = dataset.automaton.distinguishability_degree_by_partition();
    std::unique_ptr<Teacher> teacher;
    std::unique_ptr<Learner> learner;
    std::unique_ptr<LearningProtocol> protocol;
    LearnerAutomaton* model = nullptr;
    std::string protocol_name;

    if (type == ProtocolType::Reset) {
        auto concrete_teacher =
            std::make_unique<ResetTeacher>(dataset.automaton, seed);
        auto concrete_learner = std::make_unique<ResetLearner>(
            dataset.automaton.input_count(), depth);
        model = &concrete_learner->automaton();
        protocol = std::make_unique<ResetProtocol>(*concrete_teacher,
                                                   *concrete_learner);
        teacher = std::move(concrete_teacher);
        learner = std::move(concrete_learner);
        protocol_name = "RESET";
    } else {
        auto concrete_teacher =
            std::make_unique<BackTeacher>(dataset.automaton, seed);
        auto concrete_learner = std::make_unique<BackLearner>(
            dataset.automaton.input_count(), depth);
        model = &concrete_learner->automaton();
        protocol = std::make_unique<BackProtocol>(*concrete_teacher,
                                                  *concrete_learner);
        teacher = std::move(concrete_teacher);
        learner = std::move(concrete_learner);
        protocol_name = "BACK";
    }

    const auto start = std::chrono::steady_clock::now();
    protocol->reset();
    while (!learner->stopCondition()) {
        protocol->step();
        if (protocol->statistics().trials % 1'000'000 == 0) {
            std::cout << "  progress: " << dataset.name << ' ' << protocol_name
                      << " seed=" << seed
                      << " trials=" << protocol->statistics().trials << '\n';
        }
    }
    const auto stop = std::chrono::steady_clock::now();

    std::size_t complete = 0;
    std::size_t merged = 0;
    std::size_t incomplete = 0;
    count_states(*model, complete, merged, incomplete);
    const auto& statistics = protocol->statistics();

    return {
        dataset.name,
        protocol_name,
        seed,
        depth,
        statistics.trials,
        statistics.queries,
        statistics.backs,
        complete,
        merged,
        incomplete,
        model->state_count(),
        learner->stopCondition(),
        check_equivalence(dataset.automaton, *learner, *model),
        std::chrono::duration<double, std::milli>(stop - start).count(),
    };
}

Automaton load_one(const fs::path& relative_path, std::size_t expected_states) {
    const fs::path path = project_root / relative_path;
    auto automata = Automaton::read_all_from_file(path.string());
    if (automata.size() != 1) {
        throw std::runtime_error("Expected exactly one automaton in " +
                                 path.string());
    }
    if (automata.front().state_count() != expected_states) {
        throw std::runtime_error("Unexpected state count in " + path.string());
    }
    return std::move(automata.front());
}

std::vector<Dataset> load_d1() {
    const fs::path path =
        project_root / "data/random/D1testAutomata10to640expStates.txt";
    auto automata = Automaton::read_all_from_file(path.string());
    const std::vector<std::size_t> expected_sizes = {10, 20, 40, 80, 160, 320, 640};
    if (automata.size() != expected_sizes.size()) {
        throw std::runtime_error("The D1 archive must contain seven automata");
    }

    std::vector<Dataset> datasets;
    for (std::size_t i = 0; i < expected_sizes.size(); ++i) {
        if (automata[i].state_count() != expected_sizes[i]) {
            throw std::runtime_error("Unexpected D1 state count at archive index " +
                                     std::to_string(i));
        }
        datasets.push_back(
            {"D1_" + std::to_string(expected_sizes[i]), std::move(automata[i])});
    }
    return datasets;
}

std::vector<Dataset> load_labeled_walls() {
    std::vector<Dataset> datasets;
    datasets.push_back({"G1_176", load_one(
        "data/results/GridsWithDistinctWalls/grid1.txt", 176)});
    datasets.push_back({"G2_297", load_one(
        "data/results/GridsWithDistinctWalls/grid2.txt", 297)});
    datasets.push_back({"G3_176", load_one(
        "data/results/GridsWithDistinctWalls/grid3.txt", 176)});
    datasets.push_back({"G4_297", load_one(
        "data/results/GridsWithDistinctWalls/grid4.txt", 297)});
    return datasets;
}

std::vector<Dataset> load_table2_extra() {
    std::vector<Dataset> datasets;
    datasets.push_back({"G5_86", load_one(
        "data/results/NoWallsNoLabelingHelp/grid1.txt", 86)});
    datasets.push_back({"G6_191", load_one(
        "data/results/NoWallsNoLabelingHelp/grid2.txt", 191)});
    datasets.push_back({"G7_94", load_one(
        "data/results/NoWallsNoLabelingHelp/grid3.txt", 94)});
    datasets.push_back({"G8_191", load_one(
        "data/results/NoWallsNoLabelingHelp/grid4.txt", 191)});
    datasets.push_back({"G09_86", load_one(
        "data/results/NoWallsLabelinHelp/grid1.txt", 86)});
    datasets.push_back({"G10_191", load_one(
        "data/results/NoWallsLabelinHelp/grid2.txt", 191)});
    datasets.push_back({"G11_94", load_one(
        "data/results/NoWallsLabelinHelp/grid3.txt", 94)});
    datasets.push_back({"G12_191", load_one(
        "data/results/NoWallsLabelinHelp/grid4.txt", 191)});
    return datasets;
}

ExperimentGroup parse_group(const std::string& value) {
    if (value == "table1") return ExperimentGroup::Table1;
    if (value == "table2") return ExperimentGroup::Table2;
    if (value == "all") return ExperimentGroup::All;
    throw std::runtime_error("Unknown experiment group '" + value +
                             "'; expected table1, table2, or all");
}

std::string group_name(ExperimentGroup group) {
    if (group == ExperimentGroup::Table1) return "table1";
    if (group == ExperimentGroup::Table2) return "table2";
    return "all";
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y%m%d_%H%M%S");
    return out.str();
}

void write_header(std::ostream& out) {
    out << "target\tprotocol\tseed\tdepth\ttrials\t?\t!\t?+!\tcomplete"
           "\tmerged\tincomplete\tgenerated\tcompleted\tequivalent\ttime_ms\n";
}

void write_run(std::ostream& out, const RunResult& result) {
    out << result.target << '\t' << result.protocol << '\t' << result.seed << '\t'
        << result.depth << '\t' << result.trials << '\t' << result.queries << '\t'
        << result.backs << '\t' << result.queries + result.backs << '\t'
        << result.complete << '\t' << result.merged << '\t' << result.incomplete
        << '\t' << result.generated << '\t' << std::boolalpha << result.completed
        << '\t' << result.equivalent << '\t' << std::fixed << std::setprecision(3)
        << result.time_ms << '\n';
}

double sample_standard_deviation(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    double mean = 0.0;
    for (double value : values) mean += value;
    mean /= static_cast<double>(values.size());
    double squared_difference_sum = 0.0;
    for (double value : values) {
        const double difference = value - mean;
        squared_difference_sum += difference * difference;
    }
    return std::sqrt(squared_difference_sum /
                     static_cast<double>(values.size() - 1));
}

template <typename Projection>
std::pair<double, double> mean_and_sd(const std::vector<RunResult>& values,
                                      Projection projection) {
    std::vector<double> projected;
    projected.reserve(values.size());
    double sum = 0.0;
    for (const auto& value : values) {
        const double number = static_cast<double>(projection(value));
        projected.push_back(number);
        sum += number;
    }
    return {sum / static_cast<double>(projected.size()),
            sample_standard_deviation(projected)};
}

void write_summaries(std::ostream& out, const std::vector<RunResult>& results) {
    std::map<std::pair<std::string, std::string>, std::vector<RunResult>> groups;
    for (const auto& result : results) {
        groups[{result.target, result.protocol}].push_back(result);
    }

    out << "\nSUMMARY (sample standard deviation, denominator n-1)\n";
    out << "target\tprotocol\truns\tdepth"
           "\ttrials_mean\ttrials_sd\t?_mean\t?_sd\t!_mean\t!_sd"
           "\t?+!_mean\t?+!_sd\tcomplete_mean\tcomplete_sd"
           "\tmerged_mean\tmerged_sd\tincomplete_mean\tincomplete_sd"
           "\tgenerated_mean\tgenerated_sd\ttime_ms_mean\ttime_ms_sd"
           "\tcompleted_runs\tequivalent_runs\n";

    for (const auto& [key, group] : groups) {
        const auto trials = mean_and_sd(group, [](const RunResult& r) {
            return r.trials;
        });
        const auto queries = mean_and_sd(group, [](const RunResult& r) {
            return r.queries;
        });
        const auto backs = mean_and_sd(group, [](const RunResult& r) {
            return r.backs;
        });
        const auto total = mean_and_sd(group, [](const RunResult& r) {
            return r.queries + r.backs;
        });
        const auto complete = mean_and_sd(group, [](const RunResult& r) {
            return r.complete;
        });
        const auto merged = mean_and_sd(group, [](const RunResult& r) {
            return r.merged;
        });
        const auto incomplete = mean_and_sd(group, [](const RunResult& r) {
            return r.incomplete;
        });
        const auto generated = mean_and_sd(group, [](const RunResult& r) {
            return r.generated;
        });
        const auto timing = mean_and_sd(group, [](const RunResult& r) {
            return r.time_ms;
        });

        std::size_t completed_runs = 0;
        std::size_t equivalent_runs = 0;
        for (const auto& result : group) {
            completed_runs += result.completed ? 1 : 0;
            equivalent_runs += result.equivalent ? 1 : 0;
        }

        out << key.first << '\t' << key.second << '\t' << group.size() << '\t'
            << group.front().depth << std::fixed << std::setprecision(3)
            << '\t' << trials.first << '\t' << trials.second
            << '\t' << queries.first << '\t' << queries.second
            << '\t' << backs.first << '\t' << backs.second
            << '\t' << total.first << '\t' << total.second
            << '\t' << complete.first << '\t' << complete.second
            << '\t' << merged.first << '\t' << merged.second
            << '\t' << incomplete.first << '\t' << incomplete.second
            << '\t' << generated.first << '\t' << generated.second
            << '\t' << timing.first << '\t' << timing.second
            << '\t' << completed_runs << '\t' << equivalent_runs << '\n';
    }
}

void run_dataset(const Dataset& dataset,
                 const std::vector<ProtocolType>& protocols,
                 std::ofstream& output,
                 std::vector<RunResult>& results) {
    for (ProtocolType protocol : protocols) {
        for (unsigned int seed = first_seed; seed <= last_seed; ++seed) {
            std::cout << "Running " << dataset.name << ' '
                      << (protocol == ProtocolType::Reset ? "RESET" : "BACK")
                      << " seed=" << seed << "..." << std::endl;
            RunResult result = run_once(dataset, protocol, seed);
            write_run(output, result);
            output.flush();
            results.push_back(std::move(result));
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const ExperimentGroup group = argc >= 2
                                          ? parse_group(argv[1])
                                          : ExperimentGroup::All;
        const fs::path output_path = argc >= 3
                                         ? fs::path(argv[2])
                                         : project_root / "data/results" /
                                               ("manuscript_runs_" + group_name(group) +
                                                "_seeds42-51_" + timestamp() + ".txt");
        if (!output_path.parent_path().empty()) {
            fs::create_directories(output_path.parent_path());
        }
        std::ofstream output(output_path);
        if (!output) {
            throw std::runtime_error("Cannot create result file " +
                                     output_path.string());
        }

        write_header(output);
        output.flush();
        std::vector<RunResult> results;

        if (group == ExperimentGroup::Table1 || group == ExperimentGroup::All) {
            for (const auto& dataset : load_d1()) {
                run_dataset(dataset, {ProtocolType::Reset, ProtocolType::Back},
                            output, results);
            }
            for (const auto& dataset : load_labeled_walls()) {
                run_dataset(dataset, {ProtocolType::Reset, ProtocolType::Back},
                            output, results);
            }
        }

        if (group == ExperimentGroup::Table2) {
            for (const auto& dataset : load_labeled_walls()) {
                run_dataset(dataset, {ProtocolType::Back}, output, results);
            }
        }
        if (group == ExperimentGroup::Table2 || group == ExperimentGroup::All) {
            for (const auto& dataset : load_table2_extra()) {
                run_dataset(dataset, {ProtocolType::Back}, output, results);
            }
        }

        write_summaries(output, results);
        output.close();
        std::cout << "Completed " << results.size() << " runs.\nResults saved to: "
                  << fs::absolute(output_path) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "manuscript_run failed: " << error.what() << '\n';
        return 1;
    }
}
