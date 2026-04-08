//
// Created by Adrian Dediu on 04/04/2026.
//
// src/core/types.hpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace core_types {
	using StateId = std::size_t;
	using Symbol = std::size_t;
	using Output = std::string;
	using Path = std::vector<Symbol>;
}