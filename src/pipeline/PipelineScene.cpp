#include "internal.hpp"
#include "viewport.hpp"

#include <iostream>

namespace RasterCore {

bool RasterPipeline::Impl::ensureDeviceBuffer(std::string& errorMessage) {
	const size_t requiredBytes = static_cast<size_t>(width) * height * 4;

	if (deviceColorBuffer.isValid() && deviceColorBuffer.getSize() >= requiredBytes)
		return true;

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

bool RasterPipeline::Impl::setupDescriptorSetsWithSharedResources(std::string& errorMessage) {
	if (!cameraUniformBuffer.isValid()) {
		errorMessage = "RasterCore: camera uniform buffer is invalid";
		return false;
	}

	if (!options.sharedResources || !options.sharedResources->textures) {
		errorMessage = "RasterCore: shared resources not available";
		return false;
	}

	std::cout << "=== RasterPipeline: Setting up descriptor sets with SHARED resources ===" << std::endl;
	std::cout << "  Shared textures available: " << options.sharedResources->textures->size() << std::endl;

	task->enableDescriptorManager(true);
	auto* descriptorManager = task->getDescriptorManager();
	descriptorManager->clearSets();

	auto* descriptorSet = descriptorManager->createSet(0);
	constexpr uint32_t cameraBinding = 16;
	descriptorSet->addBuffer(cameraBinding, &cameraUniformBuffer,
							 renderApi::descriptor::DescriptorType::UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
	std::cout << "  Added camera uniform buffer at binding " << cameraBinding << std::endl;

	std::vector<renderApi::Texture*> texturePtrs;
	texturePtrs.reserve(options.sharedResources->textures->size());
	size_t validTexCount = 0;
	for (size_t i = 0; i < options.sharedResources->textures->size(); ++i) {
		auto& tex = (*options.sharedResources->textures)[i];
		texturePtrs.push_back(&tex);
		if (tex.isValid()) {
			validTexCount++;
			std::cout << "\tTexture[" << i << "]: valid, imageView=" << tex.getImageView() << std::endl;
		} else
			std::cout << "\tTexture[" << i << "]: INVALID!" << std::endl;
	}
	std::cout << "  Binding texture array: " << texturePtrs.size() << " total, "
			  << validTexCount << " valid, to binding 0" << std::endl;
	descriptorSet->addTextureArray(0, texturePtrs, VK_SHADER_STAGE_FRAGMENT_BIT);

	if (options.sharedResources->modelMatrixBuffer && options.sharedResources->modelMatrixBuffer->isValid()) {
		constexpr uint32_t modelMatrixBinding = 17;
		descriptorSet->addBuffer(modelMatrixBinding, options.sharedResources->modelMatrixBuffer,
								 renderApi::descriptor::DescriptorType::STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
		std::cout << "  Added model matrix storage buffer at binding " << modelMatrixBinding << std::endl;
	} else
		std::cout << "  WARNING: Model matrix buffer not available, objects won't be transformed!" << std::endl;

	std::cout << "  DescriptorSet created at address: " << descriptorSet << std::endl;
	std::cout << "=======================================================" << std::endl;

	return true;
}

// Re-link GPU resources after SharedGpuResources has been updated.
void RasterPipeline::refreshSharedResources() {
	if (!impl_ || !impl_->task)
		return;

	auto* shared = impl_->options.sharedResources;
	if (!shared || !shared->gpu)
		return;

	// Ne pas modifier la liste de buffers du GpuTask une fois qu'il est construit.
	// Les buffers GPU eux-mêmes sont recréés par l'application et pointés par
	// SharedGpuResources. Ici on ne met à jour que les paramètres de draw et les
	// descriptors pour refléter les nouvelles ressources.

	if (shared->indexBuffer && shared->indexCount > 0) {
		impl_->task->setIndexedDrawParams(shared->indexCount, 1, 0, 0, 0);
	} else if (shared->vertexBuffer && shared->vertexCount > 0) {
		impl_->task->setDrawParams(shared->vertexCount, 1, 0, 0);
	}

	std::string error;
	if (!impl_->setupDescriptorSetsWithSharedResources(error)) {
		std::cerr << "RasterPipeline::refreshSharedResources: " << error << std::endl;
	}
}

}
