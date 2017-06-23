#define VK_USE_PLATFORM_WAYLAND_KHR

#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"
#include "render/wayland-drm-server-protocol.h"

// TODO: move to header

static void destroy_drm(struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_buffer(struct wl_resource *resource) {
	// TODO
}

static void handle_buffer_destroy(struct wl_client* client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wl_buffer_interface drm_buffer_impl = {
	.destroy = handle_buffer_destroy
};

static void
authenticate(struct wl_client *client, struct wl_resource *resource, uint32_t magic)
{
	wl_drm_send_authenticated(resource);
}

static void
create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
              uint32_t name, int32_t width, int32_t height, uint32_t stride, uint32_t format)
{
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME,
		"wlroots: GEM names are not supported, use a PRIME fd instead");
}

static void
create_planar_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                     uint32_t name, int32_t width, int32_t height, uint32_t format,
                     int32_t offset0, int32_t stride0,
                     int32_t offset1, int32_t stride1,
                     int32_t offset2, int32_t stride2)
{
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT,
		"wlroots: planar buffers are not supported\n");
}

static void
create_prime_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                    int32_t fd, int32_t width, int32_t height, uint32_t format,
                    int32_t offset0, int32_t stride0,
                    int32_t offset1, int32_t stride1,
                    int32_t offset2, int32_t stride2)
{
	// TODO
	struct wlr_renderer_state *state = wl_resource_get_user_data(resource);
	assert(state);

	struct wl_resource *buf = wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buf, &drm_buffer_impl, state, destroy_buffer);
}

static const struct wl_drm_interface drm_impl = {
	.authenticate = authenticate,
	.create_buffer = create_buffer,
	.create_planar_buffer = create_planar_buffer,
	.create_prime_buffer = create_prime_buffer,
};

static void bind_drm(struct wl_client *client, void *data,
	uint32_t version, uint32_t id) {
	struct wl_resource *res = wl_resource_create(client, &wl_drm_interface, version, id);
	wl_resource_set_implementation(res, &drm_impl, data, &destroy_drm);
}

static void vulkan_begin(struct wlr_renderer_state *state,
		struct wlr_output *output) {
}

static void vulkan_end(struct wlr_renderer_state *state) {
}

static struct wlr_surface *vulkan_surface_init(struct wlr_renderer_state *state) {
	return NULL; // TODO
}

static bool vulkan_render_surface(struct wlr_renderer_state *state,
		struct wlr_surface *surface, const float (*matrix)[16]) {
	return true;
}

static void vulkan_render_quad(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
}

static void vulkan_render_ellipse(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
}

static void vulkan_destroy(struct wlr_renderer_state *state) {
}

static struct wlr_renderer_impl renderer_impl = {
	.begin = vulkan_begin,
	.end = vulkan_end,
	.surface_init = vulkan_surface_init,
	.render_with_matrix = vulkan_render_surface,
	.render_quad = vulkan_render_quad,
	.render_ellipse = vulkan_render_ellipse,
	.destroy = vulkan_destroy
};

#define VK_CALL(x) x

static bool init_instance(struct wlr_renderer_state *state) {
	uint32_t ext_count = 0;
    VkResult ret = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, NULL);
	if ((ret != VK_SUCCESS) || (ext_count == 0)) {
		wlr_log(L_ERROR, "Could not enumerate instance extensions:1");
		return false;
	}

	VkExtensionProperties *ext_props = calloc(ext_count, sizeof(VkExtensionProperties));
    ret = vkEnumerateInstanceExtensionProperties(NULL, &ext_count, ext_props);
	if (ret != VK_SUCCESS) {
		wlr_log(L_ERROR, "Could not enumerate instance extensions:2");
		return false;
    }

    const char *extensions[] = {
    	VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
		VK_KHX_EXTERNAL_MEMORY_FD_EXTENSION_NAME
    };
	unsigned int req_ext_count = sizeof(extensions)/sizeof(extensions[0]);

    for(size_t i = 0; i < req_ext_count; ++i) {
		bool found = false;
		for(size_t j = 0; j < ext_count; ++j) {
      		if (strcmp(ext_props[j].extensionName, extensions[i]) == 0) {
				found = true;
				break;
			}
      	}

		if (!found) {
			wlr_log(L_ERROR, "Could not find extension %s", extensions[i]);
			return false;
		}
    }

	free(ext_props);
    VkApplicationInfo application_info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO,
      	NULL,
      	"sway",
      	VK_MAKE_VERSION(1,0,0),
      	"wlroots",
      	VK_MAKE_VERSION(1,0,0),
      	VK_MAKE_VERSION(1,0,0)
    };

    VkInstanceCreateInfo instance_create_info = {
      	VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      	NULL,
      	0,
      	&application_info,
      	0,
      	NULL,
      	req_ext_count,
      	extensions,
    };

	ret = vkCreateInstance(&instance_create_info, NULL, &state->instance);
    if (ret != VK_SUCCESS) {
		wlr_log(L_ERROR, "Could not create instance: %d", ret);
      	return false;
    }

    return true;
}

static bool init_device(struct wlr_renderer_state *state) {
	// TODO: choose the best physical device instead of the first
	uint32_t num_devs = 1;
	VkResult ret = vkEnumeratePhysicalDevices(state->instance, &num_devs, &state->phdev);
    if (ret != VK_SUCCESS || !state->phdev) {
		wlr_log(L_ERROR, "Could not retrieve physical device");
     	return false;
    }

	// TODO: query them correctly, try to find one queue fam for both
	state->graphics_queue_fam = 0;
	state->present_queue_fam = 0;
	bool one_queue = (state->graphics_queue_fam == state->present_queue_fam);

	float prio = 1.f;
	VkDeviceQueueCreateInfo graphics_queue_info = {
		VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, // sType
    	NULL, // *pNext
    	0, // flags
    	0, // queueFamilyIndex TODO
    	1, // queueCount
    	&prio // *pQueuePriorities
	};

	VkDeviceQueueCreateInfo present_queue_info = graphics_queue_info;
	present_queue_info.queueFamilyIndex = state->present_queue_fam;

	// TODO: correct queue creation (find one or two for graphics and presenting)
	VkDeviceQueueCreateInfo queue_infos[2] = {graphics_queue_info, present_queue_info};

    const char *extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	const char *layer_name = "VK_LAYER_LUNARG_standard_validation";

    VkDeviceCreateInfo device_create_info = {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,  // sType
	  	NULL, // pNext
	  	0, // flags
	  	one_queue ? 1 : 2, // queueCreateInfoCount
	  	&queue_infos, // pQueueCreateInfos
	  	1, // enabledLayerCount
	  	&layer_name, // ppEnabledLayerNames
	  	1, // enabledExtensionCount
	  	extensions, // ppEnabledExtensionNames
		NULL // pEnabledFeatures
    };

	ret = vkCreateDevice(state->phdev, &device_create_info, NULL, &state->dev);
    if (ret != VK_SUCCESS){
	  	wlr_log(L_ERROR, "Failed to create vulkan device");
		return false;
    }

	vkGetDeviceQueue(state->dev, state->graphics_queue_fam, 0, &state->graphics_queue);
	if (one_queue) {
		state->present_queue = state->graphics_queue;
	} else {
		vkGetDeviceQueue(state->dev, state->present_queue_fam, 0, &state->present_queue);
	}

	return true;
}

static VkSwapchainKHR init_swapchain(struct wlr_renderer_state *state,
		VkSurfaceKHR surface) {

	// TODO
	VkSwapchainKHR old = VK_NULL_HANDLE;
	int width = 800;
	int height = 500;
	bool vsync = true;
	VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR color_space;

	// load functions
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR createSwapchainKHR;
	PFN_vkDestroySwapchainKHR destroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR acquireNextImageKHR;
	PFN_vkQueuePresentKHR queuePresentKHR;

	VkSwapchainCreateInfoKHR info;
	memset(&info, 0, sizeof(info));

	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.pNext = NULL;
	info.surface = surface;
	info.oldSwapchain = old;
	info.imageFormat = format;
	info.imageColorSpace = color_space;

	VkSurfaceCapabilitiesKHR caps;
	VK_CALL(getPhysicalDeviceSurfaceCapabilitiesKHR(state->phdev, surface, &caps));

	// Get available present modes
	uint32_t present_mode_count;
	VK_CALL(getPhysicalDeviceSurfacePresentModesKHR(state->phdev, surface,
		&present_mode_count, NULL));

	VkPresentModeKHR *present_modes =
		calloc(present_mode_count, sizeof(VkPresentModeKHR));
	VK_CALL(getPhysicalDeviceSurfacePresentModesKHR(state->phdev, surface,
		&present_mode_count, present_modes));

	if (caps.currentExtent.width == (uint32_t)-1) {
		info.imageExtent.width = width;
		info.imageExtent.height = height;
	} else {
		info.imageExtent.width = caps.currentExtent.width;
		info.imageExtent.height = caps.currentExtent.height;
	}

	info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

	if (!vsync) {
		for (size_t i = 0; i < present_mode_count; i++) {
			if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
				info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			} if ((info.presentMode != VK_PRESENT_MODE_MAILBOX_KHR) &&
					(present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR)) {
				info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}
		}
	}

	uint32_t pref_image_count = caps.minImageCount + 1;
	if ((caps.maxImageCount > 0) && (pref_image_count > caps.maxImageCount)) {
		pref_image_count = caps.maxImageCount;
	}

	// Find the transformation of the surface
	VkSurfaceTransformFlagsKHR preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if (!(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) {
		preTransform = caps.currentTransform;
	}

	VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	VkCompositeAlphaFlagBitsKHR alpha_flags[] = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};

	for (int i = 0; i < 4; ++i) {
		if (caps.supportedCompositeAlpha & alpha_flags[i]) {
			alpha = alpha_flags[i];
			break;
		}
	}

	VkFormatProperties format_props;
	vkGetPhysicalDeviceFormatProperties(state->phdev, format, &format_props);
	if (format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) {
		info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	info.minImageCount = pref_image_count;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	info.imageArrayLayers = 1;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.queueFamilyIndexCount = 0;
	info.pQueueFamilyIndices = NULL;
	info.oldSwapchain = old;
	info.clipped = VK_TRUE;
	info.compositeAlpha = alpha;

	VkSwapchainKHR swapchain;
	VK_CALL(createSwapchainKHR(state->dev, &info, NULL, &swapchain));

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if (oldSwapchain != VK_NULL_HANDLE)
	{
		for (uint32_t i = 0; i < imageCount; i++)
		{
			vkDestroyImageView(device, buffers[i].view, nullptr);
		}
		fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
	}
	VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL));

	// Get the swap chain images
	images.resize(imageCount);
	VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data()));

	// Get the swap chain buffers containing the image and imageview
	buffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		VkImageViewCreateInfo colorAttachmentView = {};
		colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = colorFormat;
		colorAttachmentView.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		};
		colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorAttachmentView.flags = 0;

		buffers[i].image = images[i];

		colorAttachmentView.image = buffers[i].image;

		VK_CHECK_RESULT(vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view));
	}

}

static bool init_vulkan(struct wlr_renderer_state *state) {
	if (!init_instance(state)) {
		wlr_log(L_ERROR, "Could not init vulkan instance");
		return false;
	}

	if (!init_device(state)) {
		wlr_log(L_ERROR, "Could not init vulkan device");
		return false;
	}

	return true;
}

struct wlr_renderer *wlr_vulkan_renderer_init(struct wl_display *local_display) {
	struct wlr_renderer_state *state;
	if (!(state = calloc(1, sizeof(*state)))) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	// create vulkan device and stuff
	if(!init_vulkan(state)) {
		free(state);
		return NULL;
	}

	state->drm = (struct wl_drm*)
		wl_global_create(local_display, &wl_drm_interface, 2, state, bind_drm);
	return wlr_renderer_init(state, &renderer_impl);
}
