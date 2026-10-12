#pragma once

#include "math/mat4.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RasterCore::internal {

// `filename` itself when it exists, else the first match in `searchPaths`; empty when not found.
std::filesystem::path resolveShaderPath(const std::vector<std::filesystem::path>& searchPaths, const std::string& filename);
// Reads a SPIR-V binary found through resolveShaderPath(). On failure, returns false and fills `error`.
bool loadShader(const std::vector<std::filesystem::path>& searchPaths, const std::string& filename,
                std::vector<uint32_t>& spirv, std::string& error);
std::array<float, 16> toFloatArray(const cu::math::mat4& m);

}
