#include "Helpers.hpp"

#include <fstream>
#include <ios>

namespace RasterCore::internal {

std::filesystem::path resolveShaderPath(const std::vector<std::filesystem::path>& searchPaths, const std::string& filename) {
	std::filesystem::path direct(filename);
	if (!direct.empty() && std::filesystem::exists(direct))
		return direct;

	for (const auto& base : searchPaths) {
		if (base.empty())
			continue;
		auto candidate = base / filename;
		if (std::filesystem::exists(candidate))
			return candidate;
	}

	return {};
}

bool loadShader(const std::vector<std::filesystem::path>& searchPaths, const std::string& filename,
                std::vector<uint32_t>& spirv, std::string& error) {
	const auto path = resolveShaderPath(searchPaths, filename);
	if (path.empty()) {
		error = "RasterCore: shader not found: " + filename;
		return false;
	}

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		error = "RasterCore: unable to open shader " + path.string();
		return false;
	}

	const std::streamsize size = file.tellg();
	if (size <= 0 || size % sizeof(uint32_t) != 0) {
		error = "RasterCore: invalid SPIR-V size for " + path.string();
		return false;
	}

	spirv.resize(static_cast<size_t>(size) / sizeof(uint32_t));
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(spirv.data()), size);
	if (!file) {
		error = "RasterCore: failed to read shader " + path.string();
		return false;
	}
	return true;
}

std::array<float, 16> toFloatArray(const cu::math::mat4& m) {
	std::array<float, 16> data{};
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			data[static_cast<size_t>(row) * 4 + col] = m.m[row][col];
	return data;
}

}
