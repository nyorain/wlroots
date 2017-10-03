#ifndef _WLR_RENDER_VK_INTERNAL_H
#define _WLR_RENDER_VK_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>

// State (e.g. image texture) associates with a surface.
struct wlr_vulkan_texture {
	struct wlr_texture wlr_texture;
	VkDeviceMemory memory;
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
	VkSurfaceKHR surface;
	unsigned int width;
	unsigned int height;

	unsigned int image_count;
	struct wlr_swapchain_buffer* buffers;
};

// Vulkan renderer on top of wlr_vulkan able to render onto swapchains.
struct wlr_vulkan_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_vulkan* vulkan;
	VkSampler sampler;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkRenderPass render_pass;

	VkPipeline pipeline;
	VkCommandPool command_pool;
	VkDescriptorSet descriptor_set;
	VkDescriptorPool descriptor_pool;
	VkDeviceMemory memory;
	VkBuffer ubo;
};

// Creates a swapchain for the given surface.
struct wlr_swapchain *wlr_swapchain_create(struct wlr_vulkan *vulkan,
		VkSurfaceKHR surface);

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will additionally require the swapchain and surface base extensions.
// param debug: Whether to enable debug layers and create a debug callback.
struct wlr_vulkan *wlr_vulkan_create(
		unsigned int ini_ext_count, const char **ini_exts,
		unsigned int dev_ext_count, const char **dev_exts,
		bool debug);

// TODO: move to correct header
struct wlr_renderer *wlr_vulkan_renderer_create(struct wlr_vulkan *vulkan);

void wlr_vulkan_bind_display(struct wlr_vulkan *vulkan, 
		struct wl_display *local_dpy);

#endif
