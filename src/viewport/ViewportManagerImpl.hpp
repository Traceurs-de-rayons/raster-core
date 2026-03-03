#pragma once

#include "ViewportManager.hpp"
#include "RasterCore.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

namespace RasterCore {

class Viewport;

// Private implementation for ViewportManager
struct ViewportManager::Impl {
    std::vector<std::unique_ptr<Viewport>> viewports;
    std::unordered_map<std::string, size_t> nameToIndex;
    std::unordered_map<uint32_t, size_t> idToIndex;
    Scene sharedScene;
    int nextUnnamedId;
    
    Impl();
    
    std::string generateName();
    void updateIndices();
    void removeViewportAtIndex(size_t index);
};

} // namespace RasterCore