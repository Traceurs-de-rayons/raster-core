#include "RasterCoreInternal.hpp"

#include <cstring>

namespace RasterCore::internal {

const char* pinCString(const std::string& value) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<char[]>> pinned;

    std::scoped_lock lock(mutex);
    auto it = pinned.find(value);
    if (it != pinned.end()) {
        return it->second.get();
    }

    auto buffer = std::make_unique<char[]>(value.size() + 1);
    std::memcpy(buffer.get(), value.c_str(), value.size() + 1);
    const char* ptr = buffer.get();
    pinned.emplace(value, std::move(buffer));
    return ptr;
}

std::filesystem::path resolveShaderPath(const ShaderConfig& cfg, const std::string& filename) {
    std::filesystem::path direct(filename);
    if (!direct.empty() && std::filesystem::exists(direct)) {
        return direct;
    }

    for (const auto& base : cfg.searchPaths) {
        if (base.empty()) {
            continue;
        }
        auto candidate = base / filename;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::vector<uint32_t> readSpirvFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("RasterCore: unable to open shader " + path.string());
    }

    const std::streamsize size = file.tellg();
    if (size <= 0 || size % sizeof(uint32_t) != 0) {
        throw std::runtime_error("RasterCore: invalid SPIR-V size for " + path.string());
    }

    std::vector<uint32_t> buffer(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!file) {
        throw std::runtime_error("RasterCore: failed to read shader " + path.string());
    }
    return buffer;
}

std::array<float, 16> toFloatArray(const cu::math::mat4& m) {
    std::array<float, 16> data{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            data[static_cast<size_t>(row) * 4 + col] = m.m[row][col];
        }
    }
    return data;
}

} // namespace RasterCore::internal
