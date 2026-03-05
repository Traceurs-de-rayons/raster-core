#include "Internal.hpp"

namespace RasterCore {

void RasterPipeline::Impl::updateCameraUniform() {
	if (!cameraUniformBuffer.isValid())
		return;

	internal::CameraUniform constants{};

	const float aspect = static_cast<float>(width) / static_cast<float>(height);
	const float fovRad = activeCamera.verticalFovDegrees * 3.14159265f / 180.0f;

	const cu::math::mat4 view = cu::math::lookAt(activeCamera.position, activeCamera.target, activeCamera.up);
	const cu::math::mat4 proj = cu::math::perspective(fovRad, aspect, activeCamera.nearPlane, activeCamera.farPlane);

	constants.model = internal::toFloatArray(modelTransform);
	constants.view = internal::toFloatArray(view);
	constants.proj = internal::toFloatArray(proj);

	cameraUniformBuffer.upload(&constants, sizeof(internal::CameraUniform));
}

void RasterPipeline::Impl::copyColorImageToDeviceBuffer() {
	if (!deviceColorBuffer.isValid())
		return;

	VkImage image = pipeline->getColorImage(0);
	if (image == VK_NULL_HANDLE)
		return;

	VkCommandBuffer cmd = gpu->beginOneTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = {width, height, 1};
	vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, deviceColorBuffer.getHandle(), 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	gpu->endOneTimeCommands(cmd);
}

void RasterPipeline::Impl::transitionColorImageForShaderRead() {
	VkImage image = pipeline->getColorImage(0);
	if (image == VK_NULL_HANDLE) {
		return;
	}

	VkCommandBuffer cmd = gpu->beginOneTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	gpu->endOneTimeCommands(cmd);
}

void RasterPipeline::Impl::destroyGpuTask() {
	if (task && gpu) {
		vkDeviceWaitIdle(gpu->device);
		task.reset();
		pipeline = nullptr;
	}
}

}
