//
// Created by Adrian Dediu on 23/08/2025. //NOLINT
//

#ifndef PASSIVELEARNAUTOMATAV2_LAYEREDGENERATOR_HPP
#define PASSIVELEARNAUTOMATAV2_LAYEREDGENERATOR_HPP //NOLINT
#pragma once
#include <vector>
#include <random>
#include <unordered_set>
#include <stdexcept>
#include <filesystem>
#include <fstream>

#include <string>

#include <sstream>

#include "core/automaton.hpp"

////////////////////////////////////Directed//////////////////////

enum class RandModel { AAA, Dnk };

/**
 * Return k targets for source vertex v in an n-state automaton.
 * - AAA: with replacement in [0...n-1]  (loops and collisions allowed)
 * - Dnk: without replacement in [0...n-1]\(v)  (no self-loop, k distinct)
 *
 * rng: any UniformRandomBitGenerator (e.g., std::mt19937)
 */

inline std::vector<int>
pick_targets(int v, int n, int k, RandModel model, std::mt19937& rng)
{
    if (n <= 0 || k <= 0) throw std::invalid_argument("pick_targets: n>0, k>0 required");

    std::vector<int> picks;
    picks.reserve(k);

    if (model == RandModel::AAA) {
        std::uniform_int_distribution<> U(0, n - 1);
        for (int i = 0; i < k; ++i) picks.push_back(U(rng));
        return picks;
    }

    // Dn,k (no self-loop, k distinct)
    if (k >= n) throw std::invalid_argument("pick_targets: Dn,k requires k < n");

    std::uniform_int_distribution<> U(0, n - 1);
    std::unordered_set<int> chosen;
    chosen.reserve(static_cast<size_t>(k) * 2); //for efficient search via hash

    while (static_cast<int>(picks.size()) < k) {
        int t = U(rng);
        if (t == v) continue;                     // forbid self-loop
        if (!chosen.insert(t).second) continue;   // keep distinct
        picks.push_back(t);
    }
    return picks;
}

inline void saveAutomatonPlain(const Automaton& aut,
                               const std::string& filePath,const int writeMode=std::ios::app)
{
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(filePath).parent_path());

    const int n = aut.getStateCount();
    const int k = aut.getInputSymbolCount();
    //const int mOut = aut.getOutputSymbolCount();

    std::ofstream ofs(filePath, writeMode);
    if (!ofs) throw std::runtime_error("saveAutomatonPlain: cannot open " + filePath);

    ofs << k << ' ' << n << ' ';


    // For each symbol, write the transitions of all states
    for (int sym = 0; sym < k; ++sym) {
        for (int s = 0; s < n; ++s) {
            // The Automaton does not expose a direct table; reconstruct via a 1-step path:
            int tgt = aut.getNextState(s, sym);
            ofs << tgt << ' ';
        }
    }
    ofs << ':';

    for (int s = 0; s < n; ++s) {
        ofs << aut.getOutput(s);
    }
    ofs << '\n';
}


inline void generateRandomAutomaton(Automaton& aut, int n, int k,
                             const std::string& outputsAlphabet,
                             RandModel model, std::uint32_t seed = 0)
{
    if (outputsAlphabet.empty()) throw std::invalid_argument("outputs alphabet empty");

    std::mt19937 rng(seed ? seed : std::random_device{}());
    std::uniform_int_distribution<> Out(0, static_cast<int>(outputsAlphabet.size()) - 1);

    // assign Moore outputs
    for (int s = 0; s < n; ++s)
        aut.setOutput(s, outputsAlphabet[Out(rng)]);

    for (int v = 0; v < n; ++v) {
        auto tgts = pick_targets(v, n, k, model, rng); //NOLINT
        for (int a = 0; a < k; ++a)
            aut.addTransition(a, v, tgts[a]);
    }
}


#endif //PASSIVELEARNAUTOMATAV2_LAYEREDGENERATOR_HPP //NOLINT

