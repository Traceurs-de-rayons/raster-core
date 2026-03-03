#include "RasterCore.hpp"
#include "RasterCoreInternal.hpp"

namespace RasterCore {

void* RasterPipeline::getColorImage() const {
    if (!impl_ || !impl_->pipeline) {
        return nullptr;
    }
    
    if (impl_->target != OutputTarget::Buffer) {
        return nullptr;
    }
    
    VkImage image = impl_->pipeline->getColorImage(0);
    return static_cast<void*>(image);
}

void* RasterPipeline::getColorImageView() const {
    if (!impl_ || !impl_->pipeline) {
        return nullptr;
    }
    
    if (impl_->target != OutputTarget::Buffer) {
        return nullptr;
    }
    
    VkImageView imageView = impl_->pipeline->getColorImageView(0);
    return static_cast<void*>(imageView);
}

} // namespace RasterCore