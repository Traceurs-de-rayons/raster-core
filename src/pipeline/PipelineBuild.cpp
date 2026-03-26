#include "internal.hpp"

#include <algorithm>
#include <iostream>

namespace RasterCore {

bool RasterPipeline::Impl::rebuildPipeline(std::string &errorMessage) {
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
    pipeline->setPresentMode(windowConfig.enableVsync
                                 ? VK_PRESENT_MODE_FIFO_KHR
                                 : windowConfig.presentMode);
    pipeline->setSwapchainImageCount(
        std::max(2u, windowConfig.swapchainImageCount));
  } else
    pipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::BUFFER);

  pipeline->setColorAttachmentCount(1);
  pipeline->setColorAttachmentFormat(0, colorFormat);
  pipeline->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  pipeline->setDepthStencil(true, true, VK_COMPARE_OP_LESS);
  pipeline->setRasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                          VK_FRONT_FACE_COUNTER_CLOCKWISE);
  pipeline->setColorBlendAttachment(false);
  pipeline->addVertexBinding(0, sizeof(internal::GpuVertex),
                             VK_VERTEX_INPUT_RATE_VERTEX);
  pipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               offsetof(internal::GpuVertex, position));
  pipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               offsetof(internal::GpuVertex, color));
  pipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT,
                               offsetof(internal::GpuVertex, uv));
  pipeline->addVertexAttribute(3, 0, VK_FORMAT_R32_UINT,
                               offsetof(internal::GpuVertex, textureId));
  pipeline->addVertexAttribute(4, 0, VK_FORMAT_R32_UINT,
                               offsetof(internal::GpuVertex, modelMatrixId));

  if (!configurePipelineShaders(errorMessage))
    return false;

  if (!options.sharedResources || !options.sharedResources->gpu) {
    errorMessage = "RasterCore: shared resources are required";
    return false;
  }

  std::cout << "RasterCore: Using shared GPU resources" << std::endl;
  if (!ensureCameraUniformBuffer(errorMessage))
    return false;
  if (!setupDescriptorSetsWithSharedResources(errorMessage))
    return false;
  if (options.sharedResources->vertexBuffer)
    task->addVertexBuffer(options.sharedResources->vertexBuffer);
  if (options.sharedResources->indexBuffer) {
    task->setIndexBuffer(options.sharedResources->indexBuffer,
                         VK_INDEX_TYPE_UINT32);
    task->setIndexedDrawParams(options.sharedResources->indexCount, 1, 0, 0,
                               0);
  }
  if (target == OutputTarget::Buffer)
    if (!ensureDeviceBuffer(errorMessage))
      return false;
  if (!task->build(width, height)) {
    errorMessage = "RasterCore: failed to build GPU task";
    return false;
  }

  return true;
}

bool RasterPipeline::Impl::configurePipelineShaders(std::string &errorMessage) {
  auto vertexPath =
      internal::resolveShaderPath(shaderConfig, shaderConfig.vertex);
  if (vertexPath.empty()) {
    errorMessage =
        "RasterCore: vertex shader not found: " + shaderConfig.vertex;
    return false;
  }

  auto fragmentPath =
      internal::resolveShaderPath(shaderConfig, shaderConfig.fragment);
  if (fragmentPath.empty()) {
    errorMessage =
        "RasterCore: fragment shader not found: " + shaderConfig.fragment;
    return false;
  }

  std::cout << "=== RasterPipeline: Loading shaders ===" << std::endl;
  std::cout << "  Vertex shader: " << vertexPath << std::endl;
  std::cout << "  Fragment shader: " << fragmentPath << std::endl;

  auto vertexSpirv = internal::readSpirvFile(vertexPath);
  auto fragmentSpirv = internal::readSpirvFile(fragmentPath);

  std::cout << "  Vertex SPIR-V size: " << vertexSpirv.size() << " uint32s ("
            << (vertexSpirv.size() * 4) << " bytes)" << std::endl;
  std::cout << "  Fragment SPIR-V size: " << fragmentSpirv.size()
            << " uint32s (" << (fragmentSpirv.size() * 4) << " bytes)"
            << std::endl;

  pipeline->setVertexShader(vertexSpirv);
  pipeline->setFragmentShader(fragmentSpirv);

  std::cout << "  Shaders loaded successfully" << std::endl;
  return true;
}



}
