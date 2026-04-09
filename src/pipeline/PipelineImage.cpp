#include "rasterCore.hpp"
#include "internal.hpp"

namespace RasterCore {

void* RasterPipeline::getColorImage() const {
	if (!impl_ || !impl_->mainPipeline) {
		return nullptr;
	}

	if (impl_->target != OutputTarget::Buffer) {
		return nullptr;
	}

	VkImage image = impl_->mainPipeline->getColorImage(0);
	return static_cast<void*>(image);
}

void* RasterPipeline::getColorImageView() const {
	if (!impl_ || !impl_->mainPipeline)
		return nullptr;
	if (impl_->target != OutputTarget::Buffer)
		return nullptr;

	VkImageView imageView = impl_->mainPipeline->getColorImageView(0);
	return static_cast<void*>(imageView);
}

}
