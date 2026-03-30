#include "viewport.hpp"
#include "rasterCore.hpp"
#include "viewportManager.hpp"
#include "viewportManagerImpl.hpp"
#include <vector>

void RasterCore::ViewportManager::check_resize() {
    std::vector<Viewport*> needsResize;

    for (auto& viewport : impl_->viewports) {
        if (!viewport || !viewport->isActive())
            continue;

        RasterPipeline* pipeline = viewport->getPipeline();
        if (!pipeline)
            continue;

        uint32_t newWidth = pipeline->width();
        uint32_t newHeight = pipeline->height();
        if (newWidth > 0 && newHeight > 0 &&
            (newWidth != viewport->getWidth() || newHeight != viewport->getHeight())) {
            needsResize.push_back(viewport.get());
        }
    }

    if (needsResize.empty())
        return;

    for (auto viewport : needsResize) {
        RasterPipeline* pipeline = viewport->getPipeline();
        if (pipeline)
            pipeline->pauseGpuTask();
        viewport->setActive(false);
    }

    for (auto viewport : needsResize) {
        RasterPipeline* pipeline = viewport->getPipeline();
        if (pipeline)
            pipeline->resize();
    }

    for (auto viewport : needsResize) {
        RasterPipeline* pipeline = viewport->getPipeline();
        if (pipeline)
            pipeline->resumeGpuTask();
        viewport->setActive(true);
    }
}
