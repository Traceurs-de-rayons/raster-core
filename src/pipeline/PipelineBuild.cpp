#include "internal.hpp"

#include <algorithm>
#include <iostream>

namespace RasterCore {

bool RasterPipeline::Impl::rebuildPipeline(std::string &errorMessage) {
  destroyGpuTask();

  task = std::make_unique<renderApi::gpuTask::GpuTask>("RasterCoreTask", gpu);
  task->setAutoExecute(true);

  {

  mainPipeline = task->createGraphicsPipeline("RasterCorePipeline");
  if (!mainPipeline) {
    errorMessage = "RasterCore: failed to create graphics pipeline";
    return false;
  }

  if (target == OutputTarget::Window) {
    mainPipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::SDL_SURFACE);
    mainPipeline->setSDLWindow(windowConfig.window);
    mainPipeline->setPresentMode(windowConfig.enableVsync
                                 ? VK_PRESENT_MODE_FIFO_KHR
                                 : windowConfig.presentMode);
    mainPipeline->setSwapchainImageCount(
        std::max(2u, windowConfig.swapchainImageCount));
  } else
    mainPipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::BUFFER);

  mainPipeline->setColorAttachmentCount(1);
  mainPipeline->setColorAttachmentFormat(0, colorFormat);
  mainPipeline->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  mainPipeline->setDepthStencil(true, true, VK_COMPARE_OP_LESS);
  mainPipeline->setRasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                          VK_FRONT_FACE_COUNTER_CLOCKWISE);
  mainPipeline->setColorBlendAttachment(false);
  mainPipeline->addVertexBinding(0, sizeof(Vertex),
                             VK_VERTEX_INPUT_RATE_VERTEX);
  mainPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               offsetof(Vertex, pos));
  mainPipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                               offsetof(Vertex, normal));
  mainPipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT,
                               offsetof(Vertex, uv));

  if (!configurePipelineShaders(errorMessage))
    return false;

  }
  // {

  // debugPipeline = task->createGraphicsPipeline("RasterCoreDebugPipeline");
  // if (!debugPipeline) {
  //   errorMessage = "RasterCore: failed to create debug graphics pipeline";
  //   return false;
  // }

  // if (target == OutputTarget::Window) {
  //   debugPipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::SDL_SURFACE);
  //   debugPipeline->setSDLWindow(windowConfig.window);
  //   debugPipeline->setPresentMode(windowConfig.enableVsync
  //                                ? VK_PRESENT_MODE_FIFO_KHR
  //                                : windowConfig.presentMode);
  //   debugPipeline->setSwapchainImageCount(
  //       std::max(2u, windowConfig.swapchainImageCount));
  // } else
  //   debugPipeline->setOutputTarget(renderApi::gpuTask::OutputTarget::BUFFER);

  // debugPipeline->setColorAttachmentCount(1);
  // debugPipeline->setColorAttachmentFormat(0, colorFormat);
  // debugPipeline->setDepthFormat(VK_FORMAT_D32_SFLOAT);
  // debugPipeline->setDepthStencil(true, true, VK_COMPARE_OP_LESS);
  // debugPipeline->setRasterizer(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
  //                         VK_FRONT_FACE_COUNTER_CLOCKWISE);
  // debugPipeline->setColorBlendAttachment(false);
  // debugPipeline->addVertexBinding(0, sizeof(Vertex),
  //                            VK_VERTEX_INPUT_RATE_VERTEX);
  // debugPipeline->addVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
  //                              offsetof(Vertex, pos));
  // debugPipeline->addVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
  //                              offsetof(Vertex, normal));
  // debugPipeline->addVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT,
  //                              offsetof(Vertex, uv));

  // if (!configurePipelineShaders(errorMessage))
  //   return false;

  // }

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

  task->setAutoExecute(true);
  task->registerWithGPU();

  return true;
}

bool RasterPipeline::Impl::configurePipelineShaders(std::string &errorMessage) {
  auto mainVertexPath = internal::resolveShaderPath(shaderConfig, shaderConfig.mainVertex);
  if (mainVertexPath.empty()) {
    errorMessage = "RasterCore: vertex shader not found: " + shaderConfig.mainVertex;
    return false;
  }

  auto mainFragmentPath = internal::resolveShaderPath(shaderConfig, shaderConfig.mainFragment);
  if (mainFragmentPath.empty()) {
    errorMessage = "RasterCore: fragment shader not found: " + shaderConfig.mainFragment;
    return false;
  }

  // auto debugVertexPath = internal::resolveShaderPath(shaderConfig, shaderConfig.debugVertex);
  // if (debugVertexPath.empty()) {
  //   errorMessage = "RasterCore: debug vertex shader not found: " + shaderConfig.debugVertex;
  //   return false;
  // }

  // auto debugFragmentPath = internal::resolveShaderPath(shaderConfig, shaderConfig.debugFragment);
  // if (debugFragmentPath.empty()) {
  //   errorMessage = "RasterCore: debug fragment shader not found: " + shaderConfig.debugFragment;
  //   return false;
  // }

  std::cout << "=== RasterPipeline: Loading shaders ===" << std::endl;
  std::cout << "  Vertex shader: " << mainVertexPath << std::endl;
  std::cout << "  Fragment shader: " << mainFragmentPath << std::endl;

  auto mainVertexSpirv = internal::readSpirvFile(mainVertexPath);
  auto mainFragmentSpirv = internal::readSpirvFile(mainFragmentPath);
  // auto debugVertexSpirv = internal::readSpirvFile(debugVertexPath);
  // auto debugFragmentSpirv = internal::readSpirvFile(debugFragmentPath);


  std::cout << "  Vertex SPIR-V size: " << mainVertexSpirv.size() << " uint32s ("
            << (mainFragmentSpirv.size() * 4) << " bytes)" << std::endl;
  // std::cout << "  Fragment SPIR-V size: " << debugVertexSpirv.size()
  //           << " uint32s (" << (debugFragmentSpirv.size() * 4) << " bytes)"
  //           << std::endl;

  mainPipeline->setVertexShader(mainVertexSpirv);
  mainPipeline->setFragmentShader(mainFragmentSpirv);
  // debugPipeline->setVertexShader(debugVertexSpirv);
  // debugPipeline->setFragmentShader(debugFragmentSpirv);

  std::cout << "  Shaders loaded successfully" << std::endl;
  return true;
}



}
