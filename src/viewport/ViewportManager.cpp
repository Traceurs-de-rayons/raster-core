#include "ViewportManager.hpp"
#include "ViewportManagerImpl.hpp"
#include "Viewport.hpp"
#include "ViewportImpl.hpp"
#include "RasterCore.hpp"
#include <iostream>

namespace RasterCore {

// ViewportManager::Impl implementation

ViewportManager::Impl::Impl()
    : nextUnnamedId(0)
{
}

std::string ViewportManager::Impl::generateName() {
	    return "Viewport_" + std::to_string(nextUnnamedId++);
}

void ViewportManager::Impl::updateIndices() {
    nameToIndex.clear();
    idToIndex.clear();

    for (size_t i = 0; i < viewports.size(); ++i) {
        if (viewports[i]) {
            nameToIndex[viewports[i]->getName()] = i;
            idToIndex[viewports[i]->getId()] = i;
        }
    }
}

void ViewportManager::Impl::removeViewportAtIndex(size_t index) {
    if (index >= viewports.size()) {
        return;
    }

    // Swap with last and pop
    if (index < viewports.size() - 1) {
        std::swap(viewports[index], viewports.back());
    }

    viewports.pop_back();

    // Rebuild indices
    updateIndices();
}

// ViewportManager public interface implementation

ViewportManager::ViewportManager()
    : impl_(std::make_unique<Impl>())
{
    std::cout << "ViewportManager created" << std::endl;
}

ViewportManager::~ViewportManager() {
    if (impl_) {
        std::cout << "ViewportManager destroyed" << std::endl;
    }
}

ViewportManager::ViewportManager(ViewportManager&& other) noexcept
    : impl_(std::move(other.impl_))
{
}

ViewportManager& ViewportManager::operator=(ViewportManager&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Viewport* ViewportManager::addViewport(SDL_Window* window, const std::string& name) {
    if (!impl_) {
        return nullptr;
    }

    if (!window) {
        std::cerr << "ViewportManager: Cannot add viewport - null window" << std::endl;
        return nullptr;
    }

    // Get window size
    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    std::string viewportName = name.empty() ? impl_->generateName() : name;

    // Check if name already exists
    if (impl_->nameToIndex.find(viewportName) != impl_->nameToIndex.end()) {
        std::cerr << "ViewportManager: Viewport '" << viewportName << "' already exists" << std::endl;
        return nullptr;
    }

    uint32_t id = Viewport::Impl::nextId++;
    auto viewport = Viewport::Impl::create(id, viewportName, w, h, ViewportOutput::Window, window);

    // Initialize with current scene if available
    if (!impl_->sharedScene.triangles.empty() || !impl_->sharedScene.textures.empty()) {
        viewport->updateScene(impl_->sharedScene);
    }

    auto* ptr = viewport.get();
    size_t index = impl_->viewports.size();

    impl_->nameToIndex[viewportName] = index;
    impl_->idToIndex[id] = index;
    impl_->viewports.push_back(std::move(viewport));

    std::cout << "ViewportManager: Added window viewport '" << viewportName << "' (ID: " << id << ")" << std::endl;
    return ptr;
}

Viewport* ViewportManager::addViewport(uint32_t width, uint32_t height, const std::string& name) {
    if (!impl_) {
        return nullptr;
    }

    std::string viewportName = name.empty() ? impl_->generateName() : name;

    // Check if name already exists
    if (impl_->nameToIndex.find(viewportName) != impl_->nameToIndex.end()) {
        std::cerr << "ViewportManager: Viewport '" << viewportName << "' already exists" << std::endl;
        return nullptr;
    }

    uint32_t id = Viewport::Impl::nextId++;
    auto viewport = Viewport::Impl::create(id, viewportName, width, height, ViewportOutput::Buffer, nullptr);

    // Initialize with current scene if available
    if (!impl_->sharedScene.triangles.empty() || !impl_->sharedScene.textures.empty()) {
        viewport->updateScene(impl_->sharedScene);
    }

    auto* ptr = viewport.get();
    size_t index = impl_->viewports.size();

    impl_->nameToIndex[viewportName] = index;
    impl_->idToIndex[id] = index;
    impl_->viewports.push_back(std::move(viewport));

    std::cout << "ViewportManager: Added buffer viewport '" << viewportName
              << "' (ID: " << id << ", " << width << "x" << height << ")" << std::endl;
    return ptr;
}

Viewport* ViewportManager::getViewport(const std::string& name) {
    if (!impl_) {
        return nullptr;
    }

    auto it = impl_->nameToIndex.find(name);
    if (it == impl_->nameToIndex.end()) {
        return nullptr;
    }
    return impl_->viewports[it->second].get();
}

Viewport* ViewportManager::getViewport(uint32_t id) {
    if (!impl_) {
        return nullptr;
    }

    auto it = impl_->idToIndex.find(id);
    if (it == impl_->idToIndex.end()) {
        return nullptr;
    }
    return impl_->viewports[it->second].get();
}

bool ViewportManager::removeViewport(const std::string& name) {
    if (!impl_) {
        return false;
    }

    auto it = impl_->nameToIndex.find(name);
    if (it == impl_->nameToIndex.end()) {
        return false;
    }

    size_t index = it->second;
    impl_->removeViewportAtIndex(index);

    std::cout << "ViewportManager: Removed viewport '" << name << "'" << std::endl;
    return true;
}

bool ViewportManager::removeViewport(uint32_t id) {
    if (!impl_) {
        return false;
    }

    auto it = impl_->idToIndex.find(id);
    if (it == impl_->idToIndex.end()) {
        return false;
    }

    size_t index = it->second;
    std::string name = impl_->viewports[index]->getName();
    impl_->removeViewportAtIndex(index);

    std::cout << "ViewportManager: Removed viewport (ID: " << id << ")" << std::endl;
    return true;
}

const std::vector<std::unique_ptr<Viewport>>& ViewportManager::getViewports() const {
    static const std::vector<std::unique_ptr<Viewport>> empty;
    return impl_ ? impl_->viewports : empty;
}

void ViewportManager::updateScene(const Scene& scene) {
    if (!impl_) {
        return;
    }

    impl_->sharedScene = scene;

    // Update all viewports
    for (auto& viewport : impl_->viewports) {
        if (viewport) {
            viewport->updateScene(scene);
        }
    }

    std::cout << "ViewportManager: Updated scene for " << impl_->viewports.size() << " viewports" << std::endl;
}

void ViewportManager::renderAll() {
    if (!impl_) {
        return;
    }

    for (auto& viewport : impl_->viewports) {
        if (viewport && viewport->isActive()) {
            viewport->render();
        }
    }
}

size_t ViewportManager::getViewportCount() const {
    return impl_ ? impl_->viewports.size() : 0;
}

} // namespace RasterCore
