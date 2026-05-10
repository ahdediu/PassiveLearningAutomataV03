add_executable(generateAutomata
        generateIndoorMapsMain.cpp
        GenerateRandomAutomata.hpp
        GenerateRandomAutomataManyOutputs.hpp
)

target_compile_features(generateAutomata PRIVATE cxx_std_20)

set_target_properties(generateAutomata PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

target_include_directories(generateAutomata
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(generateAutomata
        PRIVATE
        auto_v03
)
