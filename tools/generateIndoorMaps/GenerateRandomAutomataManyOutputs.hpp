//
// Created by Adrian Dediu on 07/09/2025. //NOLINT
//
#ifndef PASSIVELEARNAUTOMATAV2_GENERATERANDOMAUTOMATAMANYOUTPUTS_HPP
#define PASSIVELEARNAUTOMATAV2_GENERATERANDOMAUTOMATAMANYOUTPUTS_HPP

// C++20
//

// Grid.hpp  — header-only
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <set>
#include <fstream>
#include <iomanip>
#include <cmath>

#include <stdexcept>
#include <filesystem>

#include "core/automaton.hpp"   // expects OutputT = int

class Grid {
private:
    int gw_ = 0, gh_ = 0;
    double cellSizeM_ = 1.0;
    bool includeWalls_ = true;
    bool encodeWallVicinity_ = false;
    bool optimizeBeacons_ = false;
    enum class CellType { Open, Wall, Beacon, Initial, Special };
    struct KVdB { std::string id; double dbm = 0.0; };
    struct GPS   { double lat=0, lon=0, accM=0; bool has=false; };

    struct AveragedSignals {
        std::vector<KVdB> ble;        // beacons (id, avg dBm)
        std::vector<KVdB> wifi;       // optional
        GPS gps;                      // optional
        };

    struct Cell {
        int r = 0, c = 0;             // coordinates (row, col)
        CellType type = CellType::Open;
        AveragedSignals sig;          // reserved for your measurements
    };

    // flat raw map as chars (after load)
    std::vector<char> raw_;                 // size = gw_*gh_
    int initR_ = -1, initC_ = -1;

    // free cells in state order (state 0 is initial)
    std::vector<Cell> cells_;
    std::vector<std::pair<double, double>> beaconsMeters_;
    std::unordered_map<int,int> gridIdToState_; // (r*gw_+c)->state

    Automaton aut_;
    // Cell types from ASCII: 'w'=wall, 'b'=beacon, 'i'=initial, 's'=special, ' '=open

    // Signals you may attach later (averaged)




public:
    // Construct from a map file (one line per row). All rows must have equal width.
    // Recognized chars: 'w' wall, 'b' beacon, 'i' initial, 's' special, ' ' open.
    explicit Grid(const std::string& mapFilePath, double cellSizeM = 1.0,
                  bool includeWalls = true, bool encodeWallVicinity = false,
                  bool optimizeBeacons = false)
        : cellSizeM_(cellSizeM), includeWalls_(includeWalls), 
          encodeWallVicinity_(encodeWallVicinity), optimizeBeacons_(optimizeBeacons)
    {
        loadAsciiFile(mapFilePath);   // fills raw map, validates, finds 'i'
        buildCells();                 // builds cells_ and makes initial be state 0
        buildAutomaton();             // transitions + simple outputs
    }

    // Persist
    void save(const std::string& path, int writeMode = std::ios::trunc) const {
        namespace fs = std::filesystem;
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream ofs(path, writeMode);
        if (!ofs) throw std::runtime_error("Grid::save: cannot open " + path);


        const int n = aut_.getStateCount();
        const int k = aut_.getInputSymbolCount();


        // header
        ofs << k << ' ' << n << ' ';

        // transitions (k blocks of n integers)
        for (int sym = 0; sym < k; ++sym) {
            for (int s = 0; s < n; ++s) {
                ofs << aut_.getNextState(s, sym) << ' ';
            }
        }

        // outputs
        ofs << ':';
        for (int s = 0; s < n; ++s) {
            if (s) ofs << ' ';
            ofs << aut_.getOutput(s); // OutputT = int
        }
        ofs << '\n';

    }

// In class Grid (public):
void saveSvg(const std::string& path,
             double cellPx = 32.0,
             bool drawStateIds = true,
             bool drawWalls = true) const
{
    const int n = aut_.getStateCount();
    const int k = aut_.getInputSymbolCount(); // expected 4 (E,N,W,S)
    const double W = gw_ * cellPx;
    const double H = gh_ * cellPx;

    auto cellCX = [&](int c){ return (c + 0.5) * cellPx; };
    auto cellCY = [&](int r){ return (r + 0.5) * cellPx; };

    std::ofstream ofs(path);
    if (!ofs) throw std::runtime_error("Grid::saveSvg: cannot open " + path);

    ofs << R"(<?xml version="1.0" encoding="UTF-8" standalone="no"?>)"
        << "\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W
        << "\" height=\"" << H << "\" viewBox=\"0 0 " << W << ' ' << H << "\">\n";

    // Arrowhead marker
    ofs << R"(  <defs>
    <marker id="arrow" markerWidth="10" markerHeight="7" refX="9" refY="3.5"
            orient="auto" markerUnits="strokeWidth">
      <path d="M0,0 L10,3.5 L0,7 z" fill="#444"/>
    </marker>
  </defs>
)";

    // Background
    ofs << "  <rect x=\"0\" y=\"0\" width=\"" << W << "\" height=\"" << H
        << "\" fill=\"white\"/>\n";

    // Optional: walls
    if (drawWalls) {
        ofs << "  <g fill=\"#ddd\" stroke=\"#ccc\" stroke-width=\"1\">\n";
        for (int r = 0; r < gh_; ++r) {
            for (int c = 0; c < gw_; ++c) {
                if (raw_[id(r,c)] == 'w') {
                    ofs << "    <rect x=\"" << c*cellPx << "\" y=\"" << r*cellPx
                        << "\" width=\"" << cellPx << "\" height=\"" << cellPx << "\"/>\n";
                }
            }
        }
        ofs << "  </g>\n";
    }

    // Grid lines (thin)
    ofs << "  <g stroke=\"#eee\" stroke-width=\"1\">\n";
    for (int r = 0; r <= gh_; ++r) {
        double y = r * cellPx;
        ofs << "    <line x1=\"0\" y1=\"" << y << "\" x2=\"" << W << "\" y2=\"" << y << "\"/>\n";
    }
    for (int c = 0; c <= gw_; ++c) {
        double x = c * cellPx;
        ofs << "    <line x1=\"" << x << "\" y1=\"0\" x2=\"" << x << "\" y2=\"" << H << "\"/>\n";
    }
    ofs << "  </g>\n";

        // Connections (non-loops only, suppress doubles, no arrows)
        ofs << "  <g stroke=\"#444\" stroke-width=\"1.8\" fill=\"none\">\n";
        for (int s = 0; s < n; ++s) {
            int sr = cells_[s].r, sc = cells_[s].c;
            double sx = cellCX(sc), sy = cellCY(sr);

            for (int a = 0; a < k; ++a) {
                int t = aut_.getNextState(s, a);
                if (t == s) continue; // skip self-loops
                if (s > t) continue;  // suppress duplicates

                int tr = cells_[t].r, tc = cells_[t].c;
                double tx = cellCX(tc), ty = cellCY(tr);

                ofs << "    <line x1=\"" << sx << "\" y1=\"" << sy
                    << "\" x2=\"" << tx << "\" y2=\"" << ty << "\"/>\n";
            }
        }
        ofs << "  </g>\n";

    // States (circles) + labels
    ofs << "  <g>\n";
    for (int s = 0; s < n; ++s) {
        int r = cells_[s].r, c = cells_[s].c;
        double x = cellCX(c), y = cellCY(r);

        // color: initial highlighted
        std::string fill = (s == 0 ? "#cfe8ff" : "#f9f9f9");
        std::string stroke = (s == 0 ? "#2a6fd3" : "#666");

        ofs << "    <circle cx=\"" << x << "\" cy=\"" << y
            << "\" r=\"" << (cellPx*0.28) << "\" fill=\"" << fill
            << "\" stroke=\"" << stroke << "\" stroke-width=\"1.5\"/>\n";

        if (drawStateIds) {
            ofs << "    <text x=\"" << x << "\" y=\"" << (y + 4)
                << "\" font-size=\"" << (cellPx*0.33)
                << "\" text-anchor=\"middle\" fill=\"#222\" font-family=\"monospace\">"
                << s << "</text>\n";
        }
    }
    ofs << "  </g>\n";

    ofs << "</svg>\n";
}

void saveMap(const std::string& path) const {
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs) throw std::runtime_error("Grid::saveMap: cannot open " + path);

    std::vector<char> mapCopy = raw_;
    if (optimizeBeacons_) {
        // Remove old beacons from the copy
        for (char& ch : mapCopy) {
            if (ch == 'b') ch = ' ';
        }
        // Add new optimized beacons
        for (const auto& b : beaconsMeters_) {
            int c = static_cast<int>(std::floor(b.first / cellSizeM_));
            int r = static_cast<int>(std::floor(b.second / cellSizeM_));
            if (in(r, c)) {
                mapCopy[id(r, c)] = 'b';
            }
        }
    }

    for (int r = 0; r < gh_; ++r) {
        for (int c = 0; c < gw_; ++c) {
            ofs << mapCopy[id(r, c)];
        }
        ofs << '\n';
    }
}

private:
    void setSignals(int state, const AveragedSignals& sig) { cells_.at(state).sig = sig; }

    // ---------- load & parse ----------
    static std::string rstripCR(std::string s) {
        while (!s.empty() && (s.back()=='\r' || s.back()=='\n')) s.pop_back();
        return s;
    }
    static CellType fromChar(char ch) {
        switch (ch) {
            case 'w': return CellType::Wall;
            case 'b': return CellType::Beacon;
            case 'i': return CellType::Initial;
            case 's': return CellType::Special;
            case ' ': return CellType::Open;
            default:  throw std::invalid_argument(std::string("Unknown map char: '")+ch+"'");
        }
    }
    static bool isWall(CellType t) { return t == CellType::Wall; }

    void loadAsciiFile(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs) throw std::runtime_error("Grid: cannot open " + path);

        std::vector<std::string> rows;
        std::string line;
        while (std::getline(ifs, line)) {
            line = rstripCR(line);
            if (!line.empty()) rows.push_back(line);
        }
        if (rows.empty()) throw std::invalid_argument("Grid: empty map file");

        gh_ = (int)rows.size();
        gw_ = (int)rows.front().size();
        for (auto& r : rows)
            if ((int)r.size() != gw_) throw std::invalid_argument("Grid: ragged rows");

        raw_.assign(gw_*gh_, ' ');
        int initCount = 0;
        for (int r=0; r<gh_; ++r) {
            for (int c=0; c<gw_; ++c) {
                char ch = rows[r][c];
                (void)fromChar(ch);           // validate char
                raw_[id(r,c)] = ch;
                if (ch == 'i') { initR_ = r; initC_ = c; ++initCount; }
                if (ch == 'w' && (false)) {}  // (placeholder for extra checks)
            }
        }
        if (initCount == 0) {
            // fallback to first non-wall
            for (int r=0;r<gh_;++r) for(int c=0;c<gw_;++c){
                if (raw_[id(r,c)]!='w') { raw_[id(r,c)]='i'; initR_=r; initC_=c; initCount=1; break; }
            }
        }
        if (initCount != 1) throw std::invalid_argument("Grid: there must be exactly one 'i' cell");
    }
    //helpers for rssi
    // --- radio model constants (simple, no noise) ---
    static constexpr double kBeaconHeightM  = 2.30;  // mounted above doors
    static constexpr double kHandsetHeightM = 1.20;  // phone in hand
    static constexpr double kDzM = (kBeaconHeightM - kHandsetHeightM);
    static constexpr double ignoreSignalUnder=-94.0;

    // Log-distance path loss (fitted to your anchors):
    // RSSI(d) = max(-36,  -55 - 39*log10(d))
    static inline double rssiModel(double dMeters) {
        if (dMeters < 0.01) dMeters = 0.01;
        const double r = -55.0 - 39.0 * std::log10(dMeters);
        return std::min(-36.0, r);
    }

    static inline double distance3D(double cx, double cy, double bx, double by) {
        const double dx = cx - bx;
        const double dy = cy - by;
        return std::sqrt(dx*dx + dy*dy + kDzM*kDzM);
    }

    // Gather beacon centers in *meters* (cell center = (c+0.5, r+0.5) * cellSizeM_)
    std::vector<std::pair<double,double>> collectBeaconsMeters() const {
        if (optimizeBeacons_) {
            return autoPlaceBeacons();
        }
        std::vector<std::pair<double,double>> B;
        B.reserve(gw_ * gh_ / 8);
        for (int r = 0; r < gh_; ++r) {
            for (int c = 0; c < gw_; ++c) {
                if (raw_[id(r,c)] == 'b') {
                    const double bx = (c + 0.5) * cellSizeM_;
                    const double by = (r + 0.5) * cellSizeM_;
                    B.emplace_back(bx, by);
                }
            }
        }
        return B;
    }

    /*
     * Algorithm for optimal beacon placement to achieve Distinguishability Degree 1 (D=1):
     * 1. Initialize an empty set of beacons B.
     * 2. Identify the set of all walkable cells S (non-wall cells).
     * 3. Define the current label of each cell s in S:
     *    Label(s) = (WallVicinity(s), RSSI(s, b1), RSSI(s, b2), ...) for all bi in B.
     * 4. While there exist pairs (s1, s2) in S such that Label(s1) == Label(s2):
     *    a. If |B| >= MAX_BEACONS (e.g., 4), stop.
     *    b. For each candidate cell c in S \ B:
     *       i. Temporarily add a beacon at c.
     *       ii. Count how many pairs (s1, s2) that previously had the same label
     *            now have different labels due to the new RSSI value from c.
     *    c. Select the candidate c with the maximum distinguishing power.
     *    d. Add c to B and update Labels.
     * 5. Return the set B of beacon positions.
     */
    std::vector<std::pair<double,double>> autoPlaceBeacons() const {
        // Greedy algorithm to achieve Distinguishability Degree 1 (D=1).
        // It tries to place beacons such that every walkable cell has a unique 1-step signature.
        std::vector<std::pair<int, int>> S;
        std::unordered_map<int, int> gridToS;
        for (int r = 0; r < gh_; ++r) {
            for (int c = 0; c < gw_; ++c) {
                if (includeWalls_ || raw_[id(r, c)] != 'w') {
                    gridToS[id(r, c)] = (int)S.size();
                    S.push_back({r, c});
                }
            }
        }
        if (S.empty()) return {};

        // Precompute transitions for S (E, N, W, S)
        static const int dr[] = {0, -1, 0, 1};
        static const int dc[] = {1, 0, -1, 0};
        std::vector<std::vector<int>> transitions(S.size(), std::vector<int>(4));
        for (int i = 0; i < (int)S.size(); ++i) {
            for (int sym = 0; sym < 4; ++sym) {
                int rr = S[i].first + dr[sym];
                int cc = S[i].second + dc[sym];
                int targetIndex = i; // default self-loop
                if (in(rr, cc)) {
                    auto it = gridToS.find(id(rr, cc));
                    if (it != gridToS.end()) {
                        int tIdx = it->second;
                        bool srcIsWall = (raw_[id(S[i].first, S[i].second)] == 'w');
                        bool tgtIsWall = (raw_[id(rr, cc)] == 'w');
                        if (srcIsWall) {
                            if (!tgtIsWall) targetIndex = tIdx;
                        } else {
                            targetIndex = tIdx;
                        }
                    }
                }
                transitions[i][sym] = targetIndex;
            }
        }

        std::vector<std::string> vicinity(S.size());
        int wallCount = 0;
        for (size_t i = 0; i < S.size(); ++i) {
            if (raw_[id(S[i].first, S[i].second)] == 'w') {
                wallCount++;
                std::ostringstream oss;
                oss << "w" << std::setw(3) << std::setfill('0') << wallCount;
                vicinity[i] = oss.str();
            } else {
                if (encodeWallVicinity_) {
                    vicinity[i] = encodeVicinity(S[i].first, S[i].second);
                } else {
                    vicinity[i] = "";
                }
            }
        }

        auto get_signature = [&](const std::vector<std::string>& labels, int i) {
            std::string sig = labels[i];
            for (int sym = 0; sym < 4; ++sym) {
                sig += "|" + labels[transitions[i][sym]];
            }
            return sig;
        };

        std::vector<std::pair<int, int>> beacons;
        const int MAX_B = 16;
        std::vector<std::string> current_labels = vicinity;

        for (int b_count = 0; b_count < MAX_B; ++b_count) {
            std::vector<std::string> current_sigs(S.size());
            for (size_t i = 0; i < S.size(); ++i) current_sigs[i] = get_signature(current_labels, i);

            std::vector<std::pair<int, int>> collisions;
            for (size_t i = 0; i < S.size(); ++i) {
                for (size_t j = i + 1; j < S.size(); ++j) {
                    if (current_sigs[i] == current_sigs[j]) collisions.push_back({(int)i, (int)j});
                }
            }
            if (collisions.empty()) break;

            int bestR = -1, bestC = -1;
            int maxResolved = -1;

            // Greedy search for the next beacon position (only on non-wall cells)
            for (int cr = 0; cr < gh_; ++cr) {
                for (int cc = 0; cc < gw_; ++cc) {
                    if (raw_[id(cr, cc)] == 'w') continue;
                    bool already = false;
                    for (auto [br, bc] : beacons) if (br == cr && bc == cc) already = true;
                    if (already) continue;

                    std::vector<int> rssis(S.size(), 0);
                    double bx = (cc + 0.5) * cellSizeM_;
                    double by = (cr + 0.5) * cellSizeM_;
                    for (size_t i = 0; i < S.size(); ++i) {
                        if (raw_[id(S[i].first, S[i].second)] == 'w') continue;
                        double cx = (S[i].second + 0.5) * cellSizeM_;
                        double cy = (S[i].first + 0.5) * cellSizeM_;
                        rssis[i] = static_cast<int>(std::abs(roundToTarget(rssiModel(distance3D(cx, cy, bx, by)))));
                    }

                    int resolved = 0;
                    for (auto [i, j] : collisions) {
                        bool distinguishes = (rssis[i] != rssis[j]);
                        if (!distinguishes) {
                            for (int sym = 0; sym < 4; ++sym) {
                                if (rssis[transitions[i][sym]] != rssis[transitions[j][sym]]) {
                                    distinguishes = true;
                                    break;
                                }
                            }
                        }
                        if (distinguishes) resolved++;
                    }

                    if (resolved > maxResolved) {
                        maxResolved = resolved;
                        bestR = cr;
                        bestC = cc;
                    }
                }
            }

            if (bestR != -1 && maxResolved > 0) {
                beacons.push_back({bestR, bestC});
                double bx = (bestC + 0.5) * cellSizeM_;
                double by = (bestR + 0.5) * cellSizeM_;
                for (size_t i = 0; i < S.size(); ++i) {
                    if (raw_[id(S[i].first, S[i].second)] == 'w') continue;
                    double cx = (S[i].second + 0.5) * cellSizeM_;
                    double cy = (S[i].first + 0.5) * cellSizeM_;
                    int rssi = static_cast<int>(std::abs(roundToTarget(rssiModel(distance3D(cx, cy, bx, by)))));
                    std::ostringstream oss;
                    oss << std::setw(3) << std::setfill('0') << rssi;
                    current_labels[i] += oss.str();
                }
            } else {
                break;
            }
        }

        std::vector<std::pair<double, double>> result;
        for (auto [r, c] : beacons) {
            result.push_back({(c + 0.5) * cellSizeM_, (r + 0.5) * cellSizeM_});
        }
        return result;
    }
    static inline double roundToTarget(double x) {
        if (x < -105.0) return 0.0; // 000 for not detected
        return std::round(x / 10.0) * 10.0;
    }

    // Compute BLE signals for one cell at (r,c)
    AveragedSignals computeSignalsForCell(int r, int c,
                                          const std::vector<std::pair<double,double>>& beaconsM) const {
        AveragedSignals sig;

        // cell center in meters
        const double cx = (c + 0.5) * cellSizeM_;
        const double cy = (r + 0.5) * cellSizeM_;

        sig.ble.reserve(beaconsM.size());
        for (size_t i = 0; i < beaconsM.size(); ++i) {
            const auto& [bx, by] = beaconsM[i];
            const double d    = distance3D(cx, cy, bx, by);
            const double rssi = roundToTarget(rssiModel(d));
            sig.ble.push_back(KVdB{ "b" + std::to_string(i), rssi });
        }

        return sig;
    }
    // ---------- build state ordering (initial -> state 0) ----------
    void buildCells() {
        cells_.clear();
        cells_.reserve(gw_*gh_);
        beaconsMeters_ = collectBeaconsMeters();

        for (int r=0;r<gh_;++r) for (int c=0;c<gw_;++c) {
            char ch = raw_[id(r,c)];
            CellType t = fromChar(ch);
            if (!includeWalls_ && t == CellType::Wall) continue;

            AveragedSignals sig;
            if (t != CellType::Wall) {
                sig = computeSignalsForCell(r, c, beaconsMeters_);
            }
            cells_.push_back(Cell{r,c,t,sig});
        }
        if (cells_.empty()) throw std::invalid_argument("Grid: no cells");

        // move initial to front
        int pos=-1;
        for (int i=0;i<(int)cells_.size();++i)
            if (cells_[i].r==initR_ && cells_[i].c==initC_) { pos=i; break; }
        if (pos<0) throw std::logic_error("Grid: initial not among cells");
        if (pos!=0) std::swap(cells_[0], cells_[pos]);

        gridIdToState_.clear();
        gridIdToState_.reserve(cells_.size()*2);
        for (int s=0; s<(int)cells_.size(); ++s)
            gridIdToState_.emplace(id(cells_[s].r, cells_[s].c), s);

    }


    // ---------- automaton ----------
    void buildAutomaton() {
        const int n = (int)cells_.size();
        const int k = 4; // E,N,W,S >^<v
        aut_ = Automaton(n, k, std::map<int,std::vector<int>>{}, std::map<int,int>{});


        auto stepTo = [&](int s, int sym){
            static const int dr[] = {0, -1, 0, 1}; // E, N, W, S
            static const int dc[] = {1, 0, -1, 0};
            const auto& cs = cells_[s];
            const int rr = cs.r + dr[sym];
            const int cc = cs.c + dc[sym];

            if (!in(rr,cc)) return s;

            auto it = gridIdToState_.find(id(rr,cc));
            if (it == gridIdToState_.end()) return s;
            int target = it->second;

            if (cs.type == CellType::Wall) {
                // From wall: only move if target is NOT a wall (return to cell)
                if (cells_[target].type == CellType::Wall) return s;
                return target;
            } else {
                // From non-wall: can move to anything
                return target;
            }
        };

        std::map<int,std::string> stateDesc;
        int wallCount = 0;
        for (int s=0; s<n; ++s) {
            std::string label;
            if (cells_[s].type == CellType::Wall) {
                wallCount++;
                std::ostringstream oss;
                oss << "w" << std::setw(3) << std::setfill('0') << wallCount;
                label = oss.str();
            } else {
                std::ostringstream oss;
                const auto& bl = cells_[s].sig.ble;
                for (const auto& b : bl) {
                    int abs_rssi = static_cast<int>(std::abs(b.dbm));
                    if (abs_rssi > 999) abs_rssi = 999;
                    oss << std::setw(3) << std::setfill('0') << abs_rssi;
                }
                if (encodeWallVicinity_) {
                    oss << encodeVicinity(cells_[s].r, cells_[s].c);
                }
                label = oss.str();
            }

            aut_.setOutput(s, label.empty() ? "000" : label);
            stateDesc[s] = label.empty() ? "no-signal" : label;

            for (int sym = 0; sym < k; ++sym) {
                aut_.addTransition(sym, s, stepTo(s, sym));
            }
        }
        aut_.set_state_description(stateDesc);
        std::set<std::string> uniqueOutputs;
        for (int s=0; s<n; ++s) {
            uniqueOutputs.insert(aut_.getOutput(s));
        }

        std::ostringstream d;
        d << "grid " << gw_ << "x" << gh_
          << " | states=" << aut_.getStateCount()
          << " | labels=" << uniqueOutputs.size()
          << " | state0=initial"
          << " | outputs=BLE signal labels";

        auto part0 = aut_.partition_by_output();
        int classes0 = 0;
        for (int c : part0) if (c >= 0 && c >= classes0) classes0 = c + 1;

        if (aut_.getStateCount() == (size_t)classes0) {
            d << " | Distinguishability Degree 0";
        } else {
            auto part1 = aut_.refine_partition(part0);
            int classes1 = 0;
            for (int c : part1) if (c >= 0 && c >= classes1) classes1 = c + 1;
            if (aut_.getStateCount() == (size_t)classes1) {
                d << " | Distinguishability Degree 1";
            } else {
                d << " | Distinguishability Degree > 1";
            }
        }
        aut_.set_description(d.str());
    }

    // ---------- helpers ----------
    inline bool in(int r,int c) const { return r>=0 && r<gh_ && c>=0 && c<gw_; }
    inline int  id(int r,int c) const { return r*gw_ + c; }

    bool isWallOrOut(int r, int c) const {
        if (!in(r, c)) return true;
        return fromChar(raw_[id(r, c)]) == CellType::Wall;
    }

    std::string encodeVicinity(int r, int c) const {
        // Order: N, S, E, W to match "1010 means N and E"
        std::string res = "";
        res += (isWallOrOut(r - 1, c) ? '1' : '0'); // N
        res += (isWallOrOut(r + 1, c) ? '1' : '0'); // S
        res += (isWallOrOut(r, c + 1) ? '1' : '0'); // E
        res += (isWallOrOut(r, c - 1) ? '1' : '0'); // W
        return res;
    }
};




#endif // PASSIVELEARNAUTOMATAV2_GENERATERANDOMAUTOMATAMANYOUTPUTS_HPP