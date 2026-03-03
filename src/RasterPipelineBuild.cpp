#include "RasterCoreInternal.hpp"

#include <algorithm>

namespace RasterCore {

bool RasterPipeline::Impl::rebuildPipeline(std::string& errorMessage) {
    destroyGpuTask();

    task = std::make_unique<renderApi::gpuTask::GpuTask>("RasterCoreTask", gpu);
    pipeline = task->createGraphicsPipeline("RasterCorePipeline");
    if (!pipeline) {
        errorMessage = "RasterCore: failed to create graphics pipeline";
        return false;
    }

    if (target == OutputTarget::Window) {
        pipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::SDL_SURFACE);
        pipeline->setSDLWindow(windowConfig.window);
        pipeline->setPresentMode(windowConfig.enableVsync ? VK_PRESENT_MODE_FIFO_KHR : windowConfig.presentMode);
        pipeline->setSwapchainImageCount(std::max(2u, windowConfig.swapchainImageCount));
    } else {
        pipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::BUFFER);
    }

    pipeline->setColorAttachmentCount(1);
    pipeline->setColorAttachmentFormat(0, colorFormat);
    pipeline->setDepthFormat(VK_FORMAT_D32_SFLOAT);
    pipeline->setDepthStencil(true, true, VK_COMPARE_OP_LESS);
    pipeline->setRasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipeline->setColorBlendAttachment(false);
    pipeline->addVertexBinding(0, sizeof(internal::GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX);
    pipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(internal::GpuVertex, position));
    pipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(internal::GpuVertex, color));
    pipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(internal::GpuVertex, uv));
    pipeline->addVertexAttribute(3, 0, VK_FORMAT_R32_UINT, offsetof(internal::GpuVertex, textureId));

    if (!configurePipelineShaders(errorMessage)) {
        return false;
    }

    if (!uploadTextures(errorMessage)) {
        return false;
    }

    if (!ensureCameraUniformBuffer(errorMessage)) {
        return false;
    }

    if (!uploadSceneGeometry(errorMessage)) {
        return false;
    }

    if (!setupDescriptorSets(errorMessage)) {
        return false;
    }

    if (target == OutputTarget::Buffer) {
        if (!ensureDeviceBuffer(errorMessage)) {
            return false;
        }
    }

    if (!task->build(width, height)) {
        errorMessage = "RasterCore: failed to build GPU task";
        return false;
    }

    return true;
}

bool RasterPipeline::Impl::configurePipelineShaders(std::string& errorMessage) {
    auto vertexPath = internal::resolveShaderPath(shaderConfig, shaderConfig.vertex);
    if (vertexPath.empty()) {
        errorMessage = "RasterCore: vertex shader not found: " + shaderConfig.vertex;
        return false;
    }

    auto fragmentPath = internal::resolveShaderPath(shaderConfig, shaderConfig.fragment);
    if (fragmentPath.empty()) {
        errorMessage = "RasterCore: fragment shader not found: " + shaderConfig.fragment;
        return false;
    }

    pipeline->setVertexShader(internal::readSpirvFile(vertexPath));
    pipeline->setFragmentShader(internal::readSpirvFile(fragmentPath));
    return true;
}

} // namespace RasterCore
