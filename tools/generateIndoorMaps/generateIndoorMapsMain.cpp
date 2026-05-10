#include <filesystem>
#include <iostream>
#include "GenerateRandomAutomata.hpp"
#include "GenerateRandomAutomataManyOutputs.hpp"

int main(){

	namespace fs = std::filesystem;

    const bool INCLUDE_WALLS = false;
    const bool ENCODE_WALL_VICINITY = true;
    const bool OPTIMIZE_BEACON_PLACEMENT = true;

	// Single output file where we append all automata
	//const std::string allFile = dataDir + "/D1_aaa_k2_m2.txt";
	//const std::string allFile = dataDir + "/automataWithSinks.txt";
	//const std::string allFile = dataDir + "/D2_grid.txt";
	/* this part is for directed auto generation
	bool firstTime = true;

		for (int nStates : {10, 20, 40, 80,160,320,640,1280}) {
			Automaton genAut(nStates, 2, std::map<int, std::vector<int>>{}, std::map<int, char>{});
			generateRandomAutomaton(genAut, nStates, 2, "+-",RandModel::AAA);

			// Save to plain text under data/

			saveAutomatonPlain(genAut, allFile,firstTime ?std::ios::out : std::ios::app);
			firstTime = false;
			std::cout << "Generated: " << nStates << '\n';
		}
	*/
	 //end directed genertion
	// Try to find the project root's data directory by looking for both src/ and data/
	std::string finalDataDir = "data";
	bool found = false;
	for (const auto& prefix : {"", "../", "../../", "../../../", "../../../../"}) {
		if (fs::exists(std::string(prefix) + "src") && 
			fs::exists(std::string(prefix) + "data") && 
			fs::is_directory(std::string(prefix) + "data")) {
			finalDataDir = std::string(prefix) + "data";
			found = true;
			break;
		}
	}
	
	if (!found) {
		// Fallback: search just for data directory
		for (const auto& prefix : {"", "../", "../../", "../../../"}) {
			if (fs::exists(std::string(prefix) + "data") && fs::is_directory(std::string(prefix) + "data")) {
				finalDataDir = std::string(prefix) + "data";
				break;
			}
		}
	}
	fs::create_directories(finalDataDir);
	constexpr auto maps = std::array{
		std::string_view{"map1"},
		std::string_view{"map2"},
		//std::string_view{"map3"},
		//std::string_view{"map4"},
	};
	for (std::string_view mapFile : maps) {
		std::string_view outputSuffix = mapFile;
		if (outputSuffix.starts_with("map")) {
			outputSuffix = outputSuffix.substr(3);
		}
		std::string mapFileStr{mapFile};
		std::vector<std::string> potential_map_paths = {
			"tools/generateIndoorMaps/" + mapFileStr + ".txt",
			"../tools/generateIndoorMaps/" + mapFileStr + ".txt",
			"../../tools/generateIndoorMaps/" + mapFileStr + ".txt",
			"../../../tools/generateIndoorMaps/" + mapFileStr + ".txt"
		};

		std::string map_path;
		for (const auto& path : potential_map_paths) {
			std::ifstream f(path);
			if (f.good()) {
				map_path = path;
				break;
			}
		}

		if (map_path.empty()) {
			std::cerr << "Error: Could not find " << mapFile << ".txt in any expected location." << std::endl;
			continue;
		}

		// --- Generate automaton with many outputs (grid + BLE labels) ---
		Grid g(map_path, 1.0, INCLUDE_WALLS, ENCODE_WALL_VICINITY, OPTIMIZE_BEACON_PLACEMENT);      // 1m cells for now
		g.save(finalDataDir + "/grid" + std::string(outputSuffix) + ".txt");   // k n ... : outputs
		g.saveMap(finalDataDir + "/map" + std::string(outputSuffix) + ".txt");
		//g.saveSvg(finalDataDir + "/grid" + outputSuffix + ".svg", 36);
	}
	return 0;

}
