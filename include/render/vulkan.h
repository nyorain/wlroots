#ifndef _WLR_RENDER_VK_INTERNAL_H
#define _WLR_RENDER_VK_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render.h>

// State (e.g. image texture) associates with a surface.
struct wlr_surface_state {
	struct wlr_surface *wlr_surface;
	VkImage image;
	VkImageView imageView;
};

// Central vulkan state
struct wlr_vulkan {
	VkInstance instance;
	VkPhysicalDevice phdev;
	VkDevice dev;

	VkQueue graphics_queue;
	VkQueue present_queue;
	unsigned int graphics_queue_fam;
	unsigned int present_queue_fam;

	struct wl_drm *drm;
};

// One buffer (image texture + command buffer) of a swapchain.
struct wlr_swapchain_buffer {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	VkCommandBuffer cmdbuf;
};

// Vulkan swapchain with retrieved buffers.
struct wlr_swapchain {
	struct wlr_vulkan* vulkan;
	VkSwapchainKHR swapchain;
	VKSurfaceKHR surface;
	unsigned int width;
	unsigned int height;

	unsigned int image_count;
	struct wlr_swapchain_buffer* buffers;
};

// Vulkan renderer on top of wlr_vulkan able to render onto swapchains.
struct wlr_renderer_state {
	struct wlr_vulkan* vulkan;
	VkSampler sampler;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkCommandPool commandPool;
	VkDescriptorSet descriptor_set;
	VkDescriptorPool descriptor_pool;
};

// Creates a swapchain for the given surface.
struct wlr_swapchain *init_swapchain(struct wlr_vulkan *state,
		VkSurfaceKHR surface);

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will additionally require the swapchain and surface base extensions.
// \param debug Whether to enable debug layers and create a debug callback.
struct wlr_vulkan *wlr_vulkan_init(unsigned int ext_count, const char **exts,
	bool debug);

#endif
