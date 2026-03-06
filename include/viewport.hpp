#ifndef VIEWPORT_HPP
# define VIEWPORT_HPP

# include "rasterTypes.hpp"
# include <cstdint>
# include <string>
# include <memory>

struct	SDL_Window;

namespace RasterCore {

class	RasterPipeline;
struct	Scene;
struct	SharedGpuResources;

class Viewport {
	private:
		friend class ViewportManager;
		struct Impl;
		std::unique_ptr<Impl> impl_;
		Viewport(uint32_t id, const std::string& name, uint32_t width, uint32_t height, ViewportOutput outputType, SDL_Window* window = nullptr, SharedGpuResources* sharedResources = nullptr);

	public:
		~Viewport();

		Viewport(const Viewport&) = delete;
		Viewport& operator=(const Viewport&) = delete;
		Viewport(Viewport&&) noexcept;
		Viewport& operator=(Viewport&&) noexcept;

		uint32_t				getWidth() const;
		uint32_t				getHeight() const;
		uint32_t				getId() const;
		const std::string&		getName() const;

		void					setRenderMode(RenderMode mode);
		RenderMode				getRenderMode() const;

		void					setActive(bool active);
		bool					isActive() const;

		Camera&					getCamera();
		const Camera&			getCamera() const;

		void					updateScene(const Scene& scene);
		void					render();

		RasterPipeline*			getPipeline();
		const RasterPipeline*	getPipeline() const;

		const unsigned char*	getImageData() const;
		size_t					getImageDataSize() const;
		void*					getVulkanImage() const;
		void*					getVulkanImageView() const;
	};

}

#endif
