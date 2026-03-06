#include "viewportManager.hpp"
#include "viewportManagerImpl.hpp"
#include "viewport.hpp"
#include "viewportImpl.hpp"
#include "rasterCore.hpp"
#include "renderApi.hpp"
#include "buffer/buffer.hpp"
#include "image/image.hpp"
#include <iostream>

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

void ViewportManager::Impl::updateSharedResources(const Scene& scene) {
	if (!createSharedGpuIfNeeded()) {
		std::cerr << "ViewportManager: Cannot update shared resources - GPU not available" << std::endl;
		return;
	}

	struct GpuVertex {
		float position[3];
		float color[3];
		float uv[2];
		uint32_t textureId;
		uint32_t modelMatrixId;
	};

	std::vector<GpuVertex> vertices;
	std::vector<uint32_t> indices;

	vertices.reserve(scene.triangles.size() * 3);
	indices.reserve(scene.triangles.size() * 3);

	uint32_t nextIndex = 0;
	uint32_t minTexId = UINT32_MAX;
	uint32_t maxTexId = 0;

	int vertexDebugCount = 0;
	for (const auto& triangle : scene.triangles) {
		for (int i = 0; i < 3; ++i) {
			const auto& src = triangle.vertices[i];
			GpuVertex dst{};
			dst.position[0] = src.pos.x;
			dst.position[1] = src.pos.y;
			dst.position[2] = src.pos.z;

			dst.color[0] = 1.0f;
			dst.color[1] = 1.0f;
			dst.color[2] = 1.0f;

			dst.uv[0] = src.uv.x;
			dst.uv[1] = src.uv.y;

			dst.textureId = src.textureId;
			dst.modelMatrixId = src.modelMatrixId;

			if (src.textureId < minTexId) minTexId = src.textureId;
			if (src.textureId > maxTexId) maxTexId = src.textureId;

			vertices.push_back(dst);
			indices.push_back(nextIndex++);
		}
	}

	sharedVertexBuffer.destroy();
	sharedIndexBuffer.destroy();

	if (!vertices.empty()) {
		sharedVertexBuffer = renderApi::createVertexBuffer(sharedGpu, vertices);
		if (sharedVertexBuffer.isValid()) {
			sharedResources.vertexBuffer = &sharedVertexBuffer;
			sharedResources.vertexCount = static_cast<uint32_t>(vertices.size());
		}
	}

	if (!indices.empty()) {
		sharedIndexBuffer = renderApi::createIndexBuffer(sharedGpu, indices);
		if (sharedIndexBuffer.isValid()) {
			sharedResources.indexBuffer = &sharedIndexBuffer;
			sharedResources.indexCount = static_cast<uint32_t>(indices.size());
		}
	}

	if (!scene.modelMatrices.empty()) {
		sharedModelMatrixBuffer.destroy();
		sharedModelMatrixBuffer = renderApi::createStorageBuffer(sharedGpu, scene.modelMatrices);
		if (sharedModelMatrixBuffer.isValid()) {
			sharedResources.modelMatrixBuffer = &sharedModelMatrixBuffer;
			std::cout << "=== Model matrix buffer created: " << scene.modelMatrices.size() << " matrices ===" << std::endl;

			for (size_t i = 0; i < std::min<size_t>(6, scene.modelMatrices.size()); ++i) {
				const auto& mat = scene.modelMatrices[i];
				std::cout << "  Matrix[" << i << "] Translation: ("
						  << mat.m[3][0] << ", " << mat.m[3][1] << ", " << mat.m[3][2] << ")" << std::endl;
			}
			std::cout << "=====================================================" << std::endl;
		}
	}

	sharedTextures.clear();

	if (scene.textures.empty()) {
		std::vector<unsigned char> whitePixel = {255, 255, 255, 255};
		renderApi::Texture defaultTex = renderApi::createTexture2D(
			sharedGpu, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
			whitePixel.data(), whitePixel.size(), false
		);
		sharedTextures.push_back(std::move(defaultTex));
	} else {
		for (size_t i = 0; i < scene.textures.size(); ++i) {
			const auto& texData = scene.textures[i];
			renderApi::Texture gpuTex = renderApi::createTexture2D(
				sharedGpu, texData.width, texData.height, VK_FORMAT_R8G8B8A8_UNORM,
				texData.pixels.data(), texData.pixels.size(), false
			);

			if (!gpuTex.isValid()) {
				std::vector<unsigned char> whitePixel = {255, 255, 255, 255};
				gpuTex = renderApi::createTexture2D(
					sharedGpu, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
					whitePixel.data(), whitePixel.size(), false
				);
			}

			sharedTextures.push_back(std::move(gpuTex));
		}
	}

	sharedResources.textures = &sharedTextures;
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
	auto viewport = Viewport::Impl::create(id, viewportName, w, h, ViewportOutput::Window, window, &impl_->sharedResources);

	if (!impl_->sharedScene.triangles.empty() || !impl_->sharedScene.textures.empty())
		viewport->updateScene(impl_->sharedScene);

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
	auto viewport = Viewport::Impl::create(id, viewportName, width, height, ViewportOutput::Buffer, nullptr, &impl_->sharedResources);

	if (!impl_->sharedScene.triangles.empty() || !impl_->sharedScene.textures.empty())
		viewport->updateScene(impl_->sharedScene);

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

void ViewportManager::updateScene(const Scene& scene) {
	if (!impl_)
		return;

	impl_->sharedScene = scene;
	impl_->updateSharedResources(scene);

	for (auto& viewport : impl_->viewports)
		if (viewport)
			viewport->updateScene(scene);

	std::cout << "ViewportManager: Updated scene for " << impl_->viewports.size() << " viewports" << std::endl;
}

void ViewportManager::renderAll() {
	if (!impl_)
		return;

	for (auto& viewport : impl_->viewports)
		if (viewport && viewport->isActive())
			viewport->render();
}

size_t ViewportManager::getViewportCount() const {
	return impl_ ? impl_->viewports.size() : 0;
}

}
