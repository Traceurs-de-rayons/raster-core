#include "RasterCoreInternal.hpp"

#include <iostream>

namespace RasterCore {

bool RasterPipeline::Impl::uploadSceneGeometry(std::string& errorMessage) {
    std::vector<internal::GpuVertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve(scene.triangles.size() * 3);
    indices.reserve(scene.triangles.size() * 3);

    uint32_t minTexId = UINT32_MAX;
    uint32_t maxTexId = 0;
    uint32_t nextIndex = 0;
    for (const auto& triangle : scene.triangles) {
        for (int i = 0; i < 3; ++i) {
            const auto& src = triangle.vertices[i];
            internal::GpuVertex dst{};
            dst.position[0] = src.pos.x;
            dst.position[1] = src.pos.y;
            dst.position[2] = src.pos.z;

            dst.color[0] = 1.0f;
            dst.color[1] = 1.0f;
            dst.color[2] = 1.0f;

            dst.uv[0] = src.uv.x;
            dst.uv[1] = src.uv.y;

            dst.textureId = src.textureId;

            if (src.textureId < minTexId) minTexId = src.textureId;
            if (src.textureId > maxTexId) maxTexId = src.textureId;

            vertices.push_back(dst);
            indices.push_back(nextIndex++);
        }
    }

    if (minTexId != UINT32_MAX) {
        std::cout << "RasterCore: Vertex textureId range: " << minTexId << " to " << maxTexId << std::endl;
    }

    if (vertices.empty()) {
        vertices.push_back({{0.0f, 0.25f, 0.0f}, {1.0f, 0.2f, 0.2f}});
        vertices.push_back({{-0.25f, -0.25f, 0.0f}, {0.2f, 1.0f, 0.2f}});
        vertices.push_back({{0.25f, -0.25f, 0.0f}, {0.2f, 0.2f, 1.0f}});
        indices = {0, 1, 2};
    }

    vertexBuffer.destroy();
    indexBuffer.destroy();

    vertexBuffer = renderApi::createVertexBuffer(gpu, vertices);
    if (!vertexBuffer.isValid()) {
        errorMessage = "RasterCore: failed to upload vertex buffer";
        return false;
    }

    if (!indices.empty()) {
        indexBuffer = renderApi::createIndexBuffer(gpu, indices);
        if (!indexBuffer.isValid()) {
            errorMessage = "RasterCore: failed to upload index buffer";
            return false;
        }
    }

    task->addVertexBuffer(&vertexBuffer);
    if (!indices.empty()) {
        task->setIndexBuffer(&indexBuffer, VK_INDEX_TYPE_UINT32);
        task->setIndexedDrawParams(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    } else {
        task->setDrawParams(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    }

    return true;
}

bool RasterPipeline::Impl::uploadTextures(std::string& errorMessage) {
    for (auto& tex : gpuTextures) {
        tex.destroy();
    }
    gpuTextures.clear();

    std::cout << "RasterCore: Uploading " << scene.textures.size() << " textures..." << std::endl;

    if (scene.textures.empty()) {
        std::cout << "RasterCore: No textures in scene, creating default white texture" << std::endl;
        std::vector<unsigned char> whitePixel = {255, 255, 255, 255};
        renderApi::Texture defaultTex = renderApi::createTexture2D(
            gpu, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
            whitePixel.data(), whitePixel.size(), false
        );
        gpuTextures.push_back(std::move(defaultTex));
    } else {
        for (size_t i = 0; i < scene.textures.size(); ++i) {
            const auto& texData = scene.textures[i];
            std::cout << "  Uploading texture " << i << ": " << texData.name
                      << " (" << texData.width << "x" << texData.height
                      << ", " << texData.channels << " channels)" << std::endl;
            if (!texData.isValid()) {
                errorMessage = "RasterCore: invalid texture data for " + texData.name;
                return false;
            }

            if (texData.channels != 4) {
                std::cerr << "RasterCore: Warning - texture " << texData.name
                          << " has " << texData.channels << " channels, expected 4 (RGBA)" << std::endl;
            }

            renderApi::Texture gpuTex = renderApi::createTexture2D(
                gpu, texData.width, texData.height, VK_FORMAT_R8G8B8A8_UNORM,
                texData.pixels.data(), texData.pixels.size(), false
            );

            if (!gpuTex.isValid()) {
                errorMessage = "RasterCore: failed to create GPU texture for " + texData.name;
                return false;
            }

            gpuTextures.push_back(std::move(gpuTex));
        }
    }

    return true;
}

bool RasterPipeline::Impl::ensureDeviceBuffer(std::string& errorMessage) {
    const size_t requiredBytes = static_cast<size_t>(width) * height * 4;

    if (deviceColorBuffer.isValid() && deviceColorBuffer.getSize() >= requiredBytes) {
        return true;
    }

    deviceColorBuffer.destroy();
    deviceColorBuffer = renderApi::createStagingBuffer(gpu, requiredBytes);
    if (!deviceColorBuffer.isValid()) {
        errorMessage = "RasterCore: failed to create device color buffer";
        return false;
    }

    return true;
}

bool RasterPipeline::Impl::ensureCameraUniformBuffer(std::string& errorMessage) {
    cameraUniformBuffer.destroy();
    cameraUniformBuffer = renderApi::createUniformBuffer(gpu, sizeof(internal::CameraUniform));
    if (!cameraUniformBuffer.isValid()) {
        errorMessage = "RasterCore: failed to create camera uniform buffer";
        return false;
    }
    return true;
}

bool RasterPipeline::Impl::setupDescriptorSets(std::string& errorMessage) {
    if (!cameraUniformBuffer.isValid()) {
        errorMessage = "RasterCore: camera uniform buffer is invalid";
        return false;
    }

    std::cout << "RasterCore: Setting up descriptor sets with " << gpuTextures.size() << " textures" << std::endl;

    task->enableDescriptorManager(true);
    auto* descriptorManager = task->getDescriptorManager();
    descriptorManager->clearSets();

    auto* descriptorSet = descriptorManager->createSet(0);
    constexpr uint32_t cameraBinding = 16;
    descriptorSet->addBuffer(cameraBinding, &cameraUniformBuffer,
                             renderApi::descriptor::DescriptorType::UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);

    for (size_t i = 0; i < gpuTextures.size(); ++i) {
        std::cout << "  Binding texture " << i << " to binding " << i << std::endl;
        descriptorSet->addTexture(static_cast<uint32_t>(i), &gpuTextures[i], VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    return true;
}

} // namespace RasterCore
