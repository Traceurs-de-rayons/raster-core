#include "viewportManager.hpp"
#include "viewportManagerImpl.hpp"
#include "viewport.hpp"
#include "viewportImpl.hpp"
#include "rasterCore.hpp"
#include "renderApi.hpp"
#include "buffer/buffer.hpp"
#include "image/image.hpp"
#include <iostream>
#include <vulkan/vulkan_core.h>

namespace RasterCore {

ViewportManager::Impl::Impl()
	: nextUnnamedId(0)
{
	sharedResources.gpu = nullptr;
	sharedResources.vertexBuffer = nullptr;
	sharedResources.indexBuffer = nullptr;
	sharedResources.modelMatrixBuffer = nullptr;
	sharedResources.textures = nullptr;
	sharedResources.indexCount = 0;
	sharedResources.vertexCount = 0;
}

ViewportManager::Impl::~Impl() {
	destroySharedResources();
}

bool ViewportManager::Impl::createSharedGpuIfNeeded() {
	if (sharedGpu)
		return true;

	renderApi::instance::Config instanceConfig;
	instanceConfig.appName = "RasterCore-Shared";

	auto initResult = renderApi::initNewInstance(instanceConfig);
	if (initResult != renderApi::instance::InitInstanceResult::INIT_VK_INSTANCE_SUCCESS) {
		std::cerr << "ViewportManager: Failed to create shared Vulkan instance" << std::endl;
		return false;
	}

	sharedGpu = nullptr;
	auto* instancePtr = renderApi::getInstance(renderApi::getInstances().size() - 1);
	if (!instancePtr) {
		std::cerr << "ViewportManager: Failed to get shared instance pointer" << std::endl;
		return false;
	}

	renderApi::device::Config gpuConfig;
	gpuConfig.graphics = 1;
	gpuConfig.compute = 0;
	gpuConfig.transfer = 0;

	auto gpuResult = instancePtr->addGPU(gpuConfig);
	if (gpuResult != renderApi::device::InitDeviceResult::INIT_DEVICE_SUCCESS) {
		std::cerr << "ViewportManager: Failed to create shared GPU" << std::endl;
		return false;
	}

	sharedGpu = instancePtr->getGPU(0);
	if (!sharedGpu) {
		std::cerr << "ViewportManager: Failed to get shared GPU pointer" << std::endl;
		return false;
	}

	sharedResources.gpu = sharedGpu;

	std::cout << "ViewportManager: Created shared GPU resources" << std::endl;
	return true;
}

void ViewportManager::Impl::updateSharedResources() {
	if (!createSharedGpuIfNeeded()) {
		std::cerr << "ViewportManager: Cannot update shared resources - GPU not available" << std::endl;
		return;
	}

	if (sharedResources.vertexBuffer == nullptr || sharedResources.indexBuffer == nullptr) {
		sharedResources.vertexCount = 0;
		sharedResources.indexCount = 0;
		return;
	}
}

void ViewportManager::Impl::destroySharedResources() {
	sharedVertexBuffer.destroy();
	sharedIndexBuffer.destroy();
	sharedModelMatrixBuffer.destroy();
	sharedTextures.clear();

	sharedResources.vertexBuffer = nullptr;
	sharedResources.indexBuffer = nullptr;
	sharedResources.modelMatrixBuffer = nullptr;
	sharedResources.textures = nullptr;
	sharedResources.gpu = nullptr;

	sharedGpu = nullptr;
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
	if (index >= viewports.size())
		return;
	if (index < viewports.size() - 1)
		std::swap(viewports[index], viewports.back());
	viewports.pop_back();
	updateIndices();
}

ViewportManager::ViewportManager() : impl_(std::make_unique<Impl>())
{
	std::cout << "ViewportManager created" << std::endl;
}

ViewportManager::~ViewportManager() {
	if (impl_)
		std::cout << "ViewportManager destroyed" << std::endl;
}

ViewportManager::ViewportManager(ViewportManager&& other) noexcept : impl_(std::move(other.impl_)) {}

ViewportManager& ViewportManager::operator=(ViewportManager&& other) noexcept {
	if (this != &other)
		impl_ = std::move(other.impl_);
	return *this;
}

bool ViewportManager::init() {
	if (!impl_)
		return false;
	if (!impl_->createSharedGpuIfNeeded()) {
		std::cerr << "ViewportManager::init: Failed to create shared GPU" << std::endl;
		return false;
	}
	return true;
}

SharedGpuResources* ViewportManager::getSharedResources() {
	return impl_ ? &impl_->sharedResources : nullptr;
}

const SharedGpuResources* ViewportManager::getSharedResources() const {
	return impl_ ? &impl_->sharedResources : nullptr;
}

Viewport* ViewportManager::addViewport(SDL_Window* window, const std::string& name) {
	if (!impl_)
		return nullptr;

	if (!window) {
		std::cerr << "ViewportManager: Cannot add viewport - null window" << std::endl;
		return nullptr;
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	std::string viewportName = name.empty() ? impl_->generateName() : name;

	if (impl_->nameToIndex.find(viewportName) != impl_->nameToIndex.end()) {
		std::cerr << "ViewportManager: Viewport '" << viewportName << "' already exists" << std::endl;
		return nullptr;
	}

	uint32_t id = Viewport::Impl::nextId++;
	auto viewport = Viewport::Impl::create(id, viewportName, w, h, ViewportOutput::Window, window, &impl_->sharedResources, this);

	auto* ptr = viewport.get();
	size_t index = impl_->viewports.size();

	impl_->nameToIndex[viewportName] = index;
	impl_->idToIndex[id] = index;
	impl_->viewports.push_back(std::move(viewport));

	std::cout << "ViewportManager: Added window viewport '" << viewportName << "' (ID: " << id << ")" << std::endl;
	return ptr;
}

Viewport* ViewportManager::addViewport(uint32_t width, uint32_t height, const std::string& name) {
	if (!impl_)
		return nullptr;

	std::string viewportName = name.empty() ? impl_->generateName() : name;

	if (impl_->nameToIndex.find(viewportName) != impl_->nameToIndex.end()) {
		std::cerr << "ViewportManager: Viewport '" << viewportName << "' already exists" << std::endl;
		return nullptr;
	}

	uint32_t id = Viewport::Impl::nextId++;
	auto viewport = Viewport::Impl::create(id, viewportName, width, height, ViewportOutput::Buffer, nullptr, &impl_->sharedResources, this);

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
	if (!impl_)
		return nullptr;

	auto it = impl_->nameToIndex.find(name);
	if (it == impl_->nameToIndex.end())
		return nullptr;
	return impl_->viewports[it->second].get();
}

Viewport* ViewportManager::getViewport(uint32_t id) {
	if (!impl_)
		return nullptr;

	auto it = impl_->idToIndex.find(id);
	if (it == impl_->idToIndex.end())
		return nullptr;
	return impl_->viewports[it->second].get();
}

bool ViewportManager::removeViewport(const std::string& name) {
	if (!impl_)
		return false;

	auto it = impl_->nameToIndex.find(name);
	if (it == impl_->nameToIndex.end())
		return false;

	size_t index = it->second;
	impl_->removeViewportAtIndex(index);

	std::cout << "ViewportManager: Removed viewport '" << name << "'" << std::endl;
	return true;
}

bool ViewportManager::removeViewport(uint32_t id) {
	if (!impl_)
		return false;

	auto it = impl_->idToIndex.find(id);
	if (it == impl_->idToIndex.end())
		return false;

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

void ViewportManager::setSharedResources(SharedGpuResources* resources) {
	if (!impl_)
		return;

	if (!resources) {
		impl_->sharedResources.gpu = nullptr;
		impl_->sharedResources.vertexBuffer = nullptr;
		impl_->sharedResources.indexBuffer = nullptr;
		impl_->sharedResources.modelMatrixBuffer = nullptr;
		impl_->sharedResources.textures = nullptr;
		impl_->sharedResources.indexCount = 0;
		impl_->sharedResources.vertexCount = 0;
		return;
	}

	impl_->sharedResources = *resources;

	// Informational log only; pipelines will pick up sharedResources when rebuilt.
	std::cout << "ViewportManager: External SharedGpuResources set" << std::endl;
}

void ViewportManager::renderAll() {
	if (!impl_)
		return;

	pauseAllViewportTasks();

	if (impl_->sharedGpu) {
		vkDeviceWaitIdle(impl_->sharedGpu->device);
	}

	for (auto& viewport : impl_->viewports)
		if (viewport && viewport->isActive())
			viewport->render();

	resumeAllViewportTasks();
}

void ViewportManager::pauseAllViewportTasks() {
    std::cout << "ViewportManager: Pausing all viewport tasks" << std::endl;

	if (!impl_)
		return;

	for (auto& viewport : impl_->viewports) {
		if (!viewport)
			continue;

		auto* pipeline = viewport->getPipeline();
		if (!pipeline)
			continue;

		auto* task = pipeline->gpuTask();
		if (task)
			task->setAutoExecute(false);
	}

	for (auto& viewport : impl_->viewports) {
		if (!viewport)
			continue;

		auto* pipeline = viewport->getPipeline();
		if (!pipeline)
			continue;

		auto* task = pipeline->gpuTask();
		if (task)
			task->wait();
	}
}

void ViewportManager::resumeAllViewportTasks() {
	if (!impl_)
		return;

	// Re-enable autoexecute on all viewport tasks
	for (auto& viewport : impl_->viewports) {
		if (!viewport)
			continue;

		auto* pipeline = viewport->getPipeline();
		if (!pipeline)
			continue;

		auto* task = pipeline->gpuTask();
		if (task) {
			task->setAutoExecute(true);
		}
	}
}

size_t ViewportManager::getViewportCount() const {
	return impl_ ? impl_->viewports.size() : 0;
}

}
