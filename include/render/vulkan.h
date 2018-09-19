#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/interface.h>

// State (e.g. image texture) associates with a surface.
struct wlr_vk_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vulkan *vulkan;
	VkDeviceMemory memory;
	VkImage image;
	VkImageView image_view;
	const struct wlr_vk_pixel_format *format;
	uint32_t width;
	uint32_t height;
	bool has_alpha;
};

// Central vulkan state
struct wlr_vulkan {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;
	VkPhysicalDevice phdev;
	VkDevice dev;

	VkQueue graphics_queue;
	VkQueue present_queue;
	uint32_t graphics_queue_fam;
	uint32_t present_queue_fam;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;
	} api;
};

// One buffer (image texture + command buffer) of a swapchain.
struct wlr_vk_swapchain_buffer {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	int age;
};

// Vulkan swapchain with retrieved buffers and rendering data.
struct wlr_vk_swapchain {
	struct wlr_vk_renderer *renderer;
	VkSwapchainCreateInfoKHR create_info;
	VkSwapchainKHR swapchain;
	VkSurfaceKHR surface;

	bool readable;
	uint32_t image_count;
	struct wlr_vk_swapchain_buffer* buffers;

	// should be moved to swapchain buffer when records are not needed
	// every frame
	VkCommandBuffer cb;
};

// Vulkan renderer on top of wlr_vulkan able to render onto swapchains.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vulkan *vulkan;
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

	VkSemaphore acquire;
	VkSemaphore present;
	struct wlr_vk_render_surface *current;
	uint32_t current_id;

	// struct wl_global *wl_drm;
};

struct wlr_vk_pixel_format {
	uint32_t wl_format;
	VkFormat vk_format;
	int bpp;
	bool has_alpha;
};

struct wlr_vk_render_surface {
	struct wlr_render_surface render_surface;
	struct wlr_vk_renderer *renderer;
	VkSurfaceKHR surface;
	struct wlr_vk_swapchain swapchain;
	uint32_t width;
	uint32_t height;
};

struct wlr_vk_drm_render_surface {
	struct wlr_render_surface render_surface;
	struct wlr_vk_renderer *renderer;

	struct buffer {
		struct gbm_bo *bo;
		struct wlr_vk_swapchain_buffer buffer;
	} buffers[2];

	struct buffer *front;
	struct buffer *back;
};

// Creates a swapchain for the given surface.
bool wlr_vk_swapchain_init(struct wlr_vk_swapchain *swapchain,
		struct wlr_vk_renderer *renderer, VkSurfaceKHR surface,
		uint32_t width, uint32_t height, bool vsync);
bool wlr_vk_swapchain_resize(struct wlr_vk_swapchain *swapchain,
		uint32_t width, uint32_t height);
void wlr_vk_swapchain_destroy(struct wlr_vk_swapchain *swapchain);

// Initializes a wlr_vulkan state. This will require
// the given extensions and fail if they cannot be found.
// Will automatically require the swapchain and surface base extensions.
// param debug: Whether to enable debug layers and create a debug callback.
struct wlr_vulkan *wlr_vulkan_create(
		unsigned ini_ext_count, const char **ini_exts,
		unsigned dev_ext_count, const char **dev_exts,
		bool debug);

void wlr_vulkan_destroy(struct wlr_vulkan *vulkan);
int wlr_vulkan_find_mem_type(struct wlr_vulkan *vulkan,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

const enum wl_shm_format *get_vulkan_formats(size_t *len);
const struct wlr_vk_pixel_format *get_vulkan_format_from_wl(
	enum wl_shm_format fmt);

struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
const char *vulkan_strerror(VkResult err);

#endif

