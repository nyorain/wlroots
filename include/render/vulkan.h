#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/interface.h>

#undef VK_QUEUE_FAMILY_FOREIGN_EXT
#define VK_QUEUE_FAMILY_FOREIGN_EXT VK_QUEUE_FAMILY_EXTERNAL

struct wlr_vk_descriptor_pool;

// Central vulkan state that should only be needed once per compositor.
struct wlr_vk_instance {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;

	// will try to enable all extensions that might be used by any wlr
	// parts later on
	unsigned extension_count;
	const char **extensions;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;

		PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2;
		PFN_vkGetPhysicalDeviceFeatures2 getPhysicalDeviceFeatures2;
		PFN_vkGetPhysicalDeviceFormatProperties2 getPhysicalDeviceFormatProperties2;
		PFN_vkGetPhysicalDeviceImageFormatProperties2 getPhysicalDeviceImageFormatProperties2;
	} api;
};

// Creates and initializes a vulkan instance.
// Will try to enable the given extensions but not fail if they are not
// available which can later be checked by the caller.
// The debug parameter determines if validation layers are enabled and a
// debug messenger created.
// `compositor_name` and `compositor_version` are passed to the vulkan driver.
struct wlr_vk_instance *wlr_vk_instance_create(
	unsigned ext_count, const char **exts, bool debug,
	const char *compositor_name, unsigned compositor_version);
void wlr_vk_instance_destroy(struct wlr_vk_instance *ini);

// Logical vulkan device state.
// Ownership can be shared by multiple renderers, reference counted
// with `renderers`.
struct wlr_vk_device {
	struct wlr_vk_instance *instance;

	VkPhysicalDevice phdev;
	VkDevice dev;

	int drm_fd; // not owned

	// list of enabled extensions
	unsigned extension_count;
	const char **extensions;

	// we only ever need one queue for rendering and transfer commands
	uint32_t queue_family;
	VkQueue queue;

	struct {
		PFN_vkGetMemoryFdPropertiesKHR getMemoryFdPropertiesKHR;

		PFN_vkBindImageMemory2 bindImageMemory2;
		PFN_vkGetImageMemoryRequirements2 getImageMemoryRequirements2;
	} api;

	struct {
		bool ycbcr; // Y'CbCr samplers enabled
	} features;


	uint32_t format_prop_count;
	struct wlr_vk_format_props *format_props;
	struct wlr_drm_format_set dmabuf_render_formats;
	struct wlr_drm_format_set dmabuf_texture_formats;

	// supported formats for textures (contains only those formats
	// that support everything we need for textures)
	uint32_t shm_format_count;
	uint32_t *shm_formats; // to implement vulkan_get_shm_texture_formats
};

// Tries to find the VkPhysicalDevice for the given drm fd.
// Might find none and return VK_NULL_HANDLE.
VkPhysicalDevice wlr_vk_find_drm_phdev(struct wlr_vk_instance *ini, int drm_fd);

// Creates a device for the given instance and physical device.
// Will try to enable the given extensions but not fail if they are not
// available which can later be checked by the caller.
struct wlr_vk_device *wlr_vk_device_create(struct wlr_vk_instance *ini,
	VkPhysicalDevice phdev, unsigned ext_count, const char **exts);
void wlr_vk_device_destroy(struct wlr_vk_device *dev);

// Tries to find any memory bit for the given vulkan device that
// supports the given flags and is set in req_bits (e.g. if memory
// type 2 is ok, (req_bits & (1 << 2)) must not be 0.
// Set req_bits to 0xFFFFFFFF to allow all types.
int wlr_vk_find_mem_type(struct wlr_vk_device *device,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

struct wlr_vk_format_plane {
	unsigned bpb; // bits per block; num_blocks_per_row = width / hsub
	unsigned hsub; // subsampling x (horizontal)
	unsigned vsub; // subsampling y (vertical)
};

struct wlr_vk_format {
	uint32_t drm_format;
	VkFormat vk_format;
	bool has_alpha;
	bool ycbcr; // whether it requires a ycbcr sampler
	unsigned plane_count;
	struct wlr_vk_format_plane planes[WLR_DMABUF_MAX_PLANES];
};

// Returns all known format mappings.
// Might not be supported for gpu/usecase.
const struct wlr_vk_format *vulkan_get_format_list(size_t *len);
const struct wlr_vk_format *vulkan_get_format_from_drm(uint32_t drm_format);

struct wlr_vk_format_modifier_props {
	VkDrmFormatModifierPropertiesEXT props;
	VkExternalMemoryFeatureFlags dmabuf_flags;
	VkExtent2D max_extent;
	bool export_imported;
};

struct wlr_vk_format_props {
	struct wlr_vk_format format;
	VkExtent2D max_extent; // relevant if not created as dma_buf
	VkFormatFeatureFlags features; // relevant if not created as dma_buf

	uint32_t render_mod_count;
	struct wlr_vk_format_modifier_props *render_mods;

	uint32_t texture_mod_count;
	struct wlr_vk_format_modifier_props *texture_mods;
};

void wlr_vk_format_props_query(struct wlr_vk_device *dev,
	const struct wlr_vk_format *format);
struct wlr_vk_format_modifier_props *wlr_vk_format_props_find_modifier(
	struct wlr_vk_format_props *props, uint64_t mod, bool render);
void wlr_vk_format_props_finish(struct wlr_vk_format_props *props);

// Everything needed to support a specific ycbcr format (quite costly to do)
struct wlr_vk_ycbcr_setup {
	struct wl_list link;
	VkFormat format;
	VkSampler sampler;
	VkSamplerYcbcrConversion conversion;

	// ycbcr samplers have to be specified in the ds layout so each ycbcr
	// format/sampler setup we support needs its own ds layout.
	// Therefore each format we support also needs it own pipe layout
	// and therefore own texture pipe.
	VkDescriptorSetLayout ds_layout;
	VkPipelineLayout pipe_layout;
	VkPipeline tex_pipe;
};

struct wlr_vk_render_buffer {
	struct wlr_buffer *wlr_buffer;
	struct wlr_vk_renderer *renderer;
	struct wl_list link; // wlr_vk_renderer.buffers

	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	uint32_t mem_count;
	VkDeviceMemory memories[WLR_DMABUF_MAX_PLANES];
	bool transitioned;

	struct wl_listener buffer_destroy;
};

// Vulkan wlr_renderer implementation on top of a wlr_vk_device.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vk_device *dev;

	// TODO: we need to support multiple render formats.
	// Create something like wlr_vk_pipeline_setup holding the pipelines
	// and the renderpass for a format.
	VkFormat render_format; // used in renderpass
	VkRenderPass render_pass;

	VkCommandPool command_pool;

	VkShaderModule vert_module;
	VkShaderModule tex_frag_module;

	// Texture rendering objects for non-ycbcr textures.
	// For ycbcr, see ycrcr_setups
	VkDescriptorSetLayout ds_layout;
	VkPipeline tex_pipe;
	VkPipelineLayout pipe_layout;
	VkSampler sampler;

	VkPipeline quad_pipe;
	VkPipeline ellipse_pipe;

	VkFence fence;

	struct wlr_vk_render_buffer *current_render_buffer;

	// current frame id. Used in wlr_vk_texture.last_used
	// Increased every time a frame is ended for the renderer
	uint32_t frame;
	VkRect2D scissor; // needed for clearing

	VkCommandBuffer cb;
	bool recording_cb;

	uint32_t render_width;
	uint32_t render_height;

	size_t last_pool_size;
	struct wl_list descriptor_pools; // type wlr_vk_descriptor_pool
	struct wl_list ycbcr_setups; // type wlr_vk_ycbcr_setup

	struct wl_list destroy_textures; // wlr_vk_texture to destroy after frame
	struct wl_list foreign_textures; // wlr_vk_texture to return to foreign queue

	struct wl_list render_buffers; // wlr_vk_render_buffer

	// TODO: rework! we likely just want a single (large, pre-allocated, like 32MB)
	// buffer. Could grow it at runtime, if needed, but the better way is
	// probably just to submit pending transfers and block for completion
	// should it be exceeded.
	// Then we don't have to track allocations and can use it more like
	// a ring buffer (or rather scratchpad, as it's reset on each frame/transfer).
	struct {
		VkCommandBuffer cb;
		// VkSemaphore signal;

		bool recording;
		struct wl_list buffers; // type wlr_vk_shared_buffer
	} stage;
};

// Creates a vulkan renderer for the given device.
struct wlr_renderer *wlr_vk_renderer_create_for_device(struct wlr_vk_device *dev);

// stage utility - for uploading/retrieving data
// Gets an command buffer in recording state which is guaranteed to be
// executed before the next frame. Call submit_upload_cb to trigger submission
// manually (costly).
VkCommandBuffer wlr_vk_record_stage_cb(struct wlr_vk_renderer *renderer);

// Submits the current stage command buffer and waits until it has
// finished execution.
bool wlr_vk_submit_stage_wait(struct wlr_vk_renderer *renderer);

// Suballocates a buffer span with the given size that can be mapped
// and used as staging buffer. The allocation is implicitly released when the
// stage cb has finished execution.
struct wlr_vk_buffer_span wlr_vk_get_stage_span(
	struct wlr_vk_renderer *renderer, VkDeviceSize size);

// Tries to allocate a texture descriptor set. Will additionally
// return the pool it was allocated from when successful (for freeing it later).
struct wlr_vk_descriptor_pool *wlr_vk_alloc_texture_ds(
	struct wlr_vk_renderer *renderer, VkDescriptorSet *ds);

// Frees the given descriptor set from the pool its pool.
void wlr_vk_free_ds(struct wlr_vk_renderer *renderer,
	struct wlr_vk_descriptor_pool *pool, VkDescriptorSet ds);
struct wlr_vk_ycbcr_setup *wlr_vk_find_ycbcr_setup(
	struct wlr_vk_renderer *renderer, const struct wlr_vk_format_props *fmt,
	bool create_if_not_found);
struct wlr_vk_format_props *wlr_vk_format_from_drm(
	struct wlr_vk_device *dev, uint32_t drm_format);
struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *r);

// State (e.g. image texture) associated with a surface.
struct wlr_vk_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vk_renderer *renderer;
	uint32_t mem_count;
	VkDeviceMemory memories[WLR_DMABUF_MAX_PLANES];
	VkImage image;
	VkImageView image_view;
	const struct wlr_vk_format *format;
	VkDescriptorSet ds;
	struct wlr_vk_descriptor_pool *ds_pool;
	uint32_t last_used; // to track when it can be destroyed
	bool dmabuf_imported;
	bool owned; // if dmabuf_imported: whether we have ownership of the image
	bool transitioned; // if dma_imported: whether we transitioned it away from preinit
	struct wl_list foreign_link;
	struct wl_list destroy_link;
};

struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
struct wlr_texture *vulkan_texture_from_pixels(
	struct wlr_renderer *wlr_renderer, uint32_t drm_fmt,
	uint32_t stride, uint32_t width, uint32_t height, const void *data);
VkImage vulkan_import_dmabuf(struct wlr_vk_renderer *renderer,
	const struct wlr_dmabuf_attributes *attribs,
	VkDeviceMemory mems[static WLR_DMABUF_MAX_PLANES], uint32_t *n_mems,
	bool for_render);
struct wlr_texture *vulkan_texture_from_dmabuf(
	struct wlr_renderer *wlr_renderer,
	struct wlr_dmabuf_attributes *attribs);

struct wlr_vk_descriptor_pool {
	VkDescriptorPool pool;
	uint32_t free; // number of textures that can be allocated
	struct wl_list link;
};

struct wlr_vk_allocation {
	VkDeviceSize start;
	VkDeviceSize size;
};

// List of suballocated staging buffers.
// Used to upload to/read from device local images.
struct wlr_vk_shared_buffer {
	struct wl_list link;
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize buf_size;

	size_t allocs_size;
	size_t allocs_capacity;
	struct wlr_vk_allocation *allocs;
};

// Suballocated range on a buffer.
struct wlr_vk_buffer_span {
	struct wlr_vk_shared_buffer *buffer;
	struct wlr_vk_allocation alloc;
};

// util
bool vulkan_has_extension(unsigned count, const char **exts, const char *find);
const char *vulkan_strerror(VkResult err);
void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
	VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
	VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta);
void vulkan_change_layout_queue(VkCommandBuffer cb, VkImage img,
	VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
	VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta,
	uint32_t src_family, uint32_t dst_family);

#define wlr_vk_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

#endif // RENDER_VULKAN_H
