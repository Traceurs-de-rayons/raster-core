#include "viewport.hpp"
#include "viewportImpl.hpp"
#include "rasterCore.hpp"
#include <iostream>

namespace RasterCore {

uint32_t Viewport::Impl::nextId = 0;

Viewport::Impl::Impl(uint32_t id, const std::string& name, uint32_t width, uint32_t height,
					 ViewportOutput outputType, SDL_Window* window, SharedGpuResources* sharedResources)
	: id(id)
	, name(name)
	, width(width)
	, height(height)
	, outputType(outputType)
	, window(window)
	, renderMode(RenderMode::Normal)
	, active(true)
	, camera{}
	, sharedResources(sharedResources)
{
	camera.position = {0.0f, 2.0f, 5.0f};
	camera.target = {0.0f, 0.0f, 0.0f};
	camera.up = {0.0f, 1.0f, 0.0f};
	camera.verticalFovDegrees = 60.0f;
	camera.nearPlane = 0.1f;
	camera.farPlane = 1000.0f;
}

bool Viewport::Impl::initializePipeline() {
	if (pipeline)
		return true;

	InitOptions options;
	options.sharedResources = sharedResources;

	if (outputType == ViewportOutput::Window) {
		if (!window) {
			std::cerr << "Viewport: Cannot initialize - no window provided" << std::endl;
			return false;
		}

		options.target = OutputTarget::Window;
		options.window.window = window;
		options.window.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		options.window.swapchainImageCount = 3;
	} else {
		options.target = OutputTarget::Buffer;
		options.buffer.width = width;
		options.buffer.height = height;
		options.buffer.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
	}

	// We don't use RasterCore::Scene anymore; geometry comes from SharedGpuResources only.
	// Keep the Scene parameter for legacy API compatibility but ignore it internally.
	Scene dummyScene{};
	auto result = initRasterisation(dummyScene, options);
	if (!result.success || !result.pipeline) {
		std::cerr << "Viewport: Failed to initialize pipeline: " << result.errorMessage << std::endl;
		return false;
	}

	pipeline = result.pipeline;

	std::cout << "Viewport '" << name << "': Pipeline initialized" << std::endl;
	return true;
}

std::unique_ptr<Viewport> Viewport::Impl::create(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window, SharedGpuResources* sharedResources) {
	return std::unique_ptr<Viewport>(new Viewport(id, name, width, height, outputType, window, sharedResources));
}

Viewport::Viewport(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window, SharedGpuResources* sharedResources)
	: impl_(std::make_unique<Impl>(id, name, width, height, outputType, window, sharedResources))
{
	// Build pipeline immediately for this viewport so that getVulkanImage/View
	// is ready when the UI first queries it.
	if (impl_) {
		if (!impl_->initializePipeline()) {
			std::cerr << "Viewport '" << name << "': Failed to initialize pipeline on creation" << std::endl;
		}
	}
	std::cout << "Viewport '" << impl_->name << "' created (ID: " << impl_->id << ", " << impl_->width << "x" << impl_->height << ")" << std::endl;
}

Viewport::~Viewport() {
	if (impl_)
		std::cout << "Viewport '" << impl_->name << "' destroyed (ID: " << impl_->id << ")" << std::endl;
}

Viewport::Viewport(Viewport&& other) noexcept : impl_(std::move(other.impl_)) {}

Viewport& Viewport::operator=(Viewport&& other) noexcept {
	if (this != &other)
		impl_ = std::move(other.impl_);
	return *this;
}

uint32_t Viewport::getWidth() const {
	return impl_ ? impl_->width : 0;
}

uint32_t Viewport::getHeight() const {
	return impl_ ? impl_->height : 0;
}

uint32_t Viewport::getId() const {
	return impl_ ? impl_->id : 0;
}

const std::string& Viewport::getName() const {
	static const std::string empty;
	return impl_ ? impl_->name : empty;
}

void Viewport::setRenderMode(RenderMode mode) {
	if (impl_)
		impl_->renderMode = mode;
}

RenderMode Viewport::getRenderMode() const {
	return impl_ ? impl_->renderMode : RenderMode::Normal;
}

void Viewport::setActive(bool active) {
	if (impl_)
		impl_->active = active;
}

bool Viewport::isActive() const {
	return impl_ ? impl_->active : false;
}

Camera& Viewport::getCamera() {
	static Camera dummy;
	return impl_ ? impl_->camera : dummy;
}

const Camera& Viewport::getCamera() const {
	static Camera dummy;
	return impl_ ? impl_->camera : dummy;
}


void Viewport::render() {
	if (!impl_ || !impl_->active)
		return;

	// Lazy safety: ensure pipeline is built even if constructor init failed earlier.
	if (!impl_->pipeline) {
		if (!impl_->initializePipeline())
			return;
	}

	impl_->pipeline->setCamera(impl_->camera);
	impl_->pipeline->drawFrame();
}

RasterPipeline* Viewport::getPipeline() {
	return impl_ ? impl_->pipeline.get() : nullptr;
}

const RasterPipeline* Viewport::getPipeline() const {
	return impl_ ? impl_->pipeline.get() : nullptr;
}

const unsigned char* Viewport::getImageData() const {
	if (!impl_ || !impl_->pipeline || impl_->outputType != ViewportOutput::Buffer)
		return nullptr;

	auto* buffer = impl_->pipeline->deviceBuffer();
	if (!buffer)
		return nullptr;
	return nullptr;
}

size_t Viewport::getImageDataSize() const {
	if (!impl_)
		return 0;
	return static_cast<size_t>(impl_->width) * impl_->height * 4;
}

void* Viewport::getVulkanImage() const {
	if (!impl_ || !impl_->pipeline || impl_->outputType != ViewportOutput::Buffer)
		return nullptr;
	return impl_->pipeline->getColorImage();
}

void* Viewport::getVulkanImageView() const {
	if (!impl_ || !impl_->pipeline || impl_->outputType != ViewportOutput::Buffer)
		return nullptr;
	return impl_->pipeline->getColorImageView();
}

}
