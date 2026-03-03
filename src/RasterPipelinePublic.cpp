#include "RasterCoreInternal.hpp"

#include <iostream>

namespace RasterCore {

RasterPipeline::RasterPipeline(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {
}

RasterPipeline::~RasterPipeline() {
    if (!impl_) {
        return;
    }

    impl_->destroyGpuTask();
    impl_->vertexBuffer.destroy();
    impl_->indexBuffer.destroy();
    impl_->deviceColorBuffer.destroy();
    impl_->cameraUniformBuffer.destroy();
}

RasterPipeline::RasterPipeline(RasterPipeline&&) noexcept = default;
RasterPipeline& RasterPipeline::operator=(RasterPipeline&&) noexcept = default;

void RasterPipeline::drawFrame() {
    if (!impl_ || !impl_->task) {
        return;
    }

    impl_->updateCameraUniform();
    impl_->task->execute();

    if (impl_->target == OutputTarget::Window && impl_->pipeline) {
        uint32_t newWidth = impl_->pipeline->getWidth();
        uint32_t newHeight = impl_->pipeline->getHeight();
        if (newWidth > 0 && newHeight > 0 &&
            (newWidth != impl_->width || newHeight != impl_->height)) {
            impl_->width = newWidth;
            impl_->height = newHeight;

            std::string error;
            if (!impl_->rebuildPipeline(error)) {
                std::cerr << error << std::endl;
            }
        }
    }

    if (impl_->target == OutputTarget::Buffer) {
        impl_->copyColorImageToDeviceBuffer();
    }
}

void RasterPipeline::waitIdle() {
    if (impl_ && impl_->gpu) {
        vkDeviceWaitIdle(impl_->gpu->device);
    }
}

void RasterPipeline::updateScene(const Scene& newScene) {
    if (!impl_) {
        return;
    }

    impl_->scene = newScene;
    impl_->activeCamera = newScene.camera;

    std::string error;
    if (!impl_->rebuildPipeline(error)) {
        throw std::runtime_error(error);
    }
}

const Scene& RasterPipeline::scene() const {
    static Scene emptyScene;
    return impl_ ? impl_->scene : emptyScene;
}

void RasterPipeline::setModelTransform(const cu::math::mat4& transform) {
    if (impl_) {
        impl_->modelTransform = transform;
    }
}

cu::math::mat4 RasterPipeline::getModelTransform() const {
    if (impl_) {
        return impl_->modelTransform;
    }
    return cu::math::mat4::identity();
}

OutputTarget RasterPipeline::target() const {
    return impl_ ? impl_->target : OutputTarget::Buffer;
}

uint32_t RasterPipeline::width() const {
    return impl_ ? impl_->width : 0;
}

uint32_t RasterPipeline::height() const {
    return impl_ ? impl_->height : 0;
}

renderApi::Buffer* RasterPipeline::deviceBuffer() {
    if (!impl_ || impl_->target != OutputTarget::Buffer) {
        return nullptr;
    }
    return &impl_->deviceColorBuffer;
}

renderApi::gpuTask::GpuTask* RasterPipeline::gpuTask() {
    return impl_ ? impl_->task.get() : nullptr;
}

renderApi::device::GPU* RasterPipeline::gpu() {
    return impl_ ? impl_->gpu : nullptr;
}

VkRenderPass RasterPipeline::getRenderPass() {
    if (!impl_ || !impl_->pipeline) {
        return VK_NULL_HANDLE;
    }
    return impl_->pipeline->getRenderPass();
}

void RasterPipeline::setCamera(const Camera& camera) {
    if (impl_) {
        impl_->activeCamera = camera;
    }
}

Camera RasterPipeline::getCamera() const {
    if (impl_) {
        return impl_->activeCamera;
    }
    return {};
}

InitResult initRasterisation(const Scene& scene, const InitOptions& options) {
    InitResult result;
    auto impl = std::make_unique<RasterPipeline::Impl>();
    if (!impl->initialize(scene, options, result.errorMessage)) {
        result.success = false;
        return result;
    }

    result.pipeline = std::shared_ptr<RasterPipeline>(new RasterPipeline(std::move(impl)));
    result.success = true;
    return result;
}

} // namespace RasterCore
