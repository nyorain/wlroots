#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <render/vulkan.h>
#include "wayland-drm-server-protocol.h"

#include "shaders/texture.vert.h"
#include "shaders/texture.frag.h"

// macros for loading function pointers
/*
#define WLR_VK_PROC_DEV(dev, name) \
	PFN_vk##name fp##name = (PFN_vk##name) vkGetDeviceProcAddr(dev, "vk" #name);

#define WLR_VK_PROC_INI(ini, name) \
	PFN_vk##name fp##name = (PFN_vk##name) vkGetInstanceProcAddr(ini, "vk" #name);
*/

// renderer
static void vulkan_begin(struct wlr_renderer *renderer,
		struct wlr_output *output) {
	// something like: retrieve swapchain from output,
	// acquire image and begin recording the command buffer
	// maybe acquire image later on
}

static void vulkan_end(struct wlr_renderer *renderer) {
	// finish command buffer recording (& renderpass)
	// maybe acquire image here (if not in begin)
	// present image to swapchain
}

static struct wlr_texture *vulkan_texture_create(struct wlr_renderer *renderer) {
	return NULL; // TODO
}

static bool vulkan_render_surface(struct wlr_renderer *renderer,
		struct wlr_texture *texture, const float (*matrix)[16]) {
	// bind pipeline (if not already bound)
	// write matrix into ubo, update descriptor set for texture
	//  even better: every texture has its own descriptor set (with bound image)
	// render shit

	return true;
}

static void vulkan_render_quad(struct wlr_renderer *renderer,
		const float (*color)[4], const float (*matrix)[16]) {
	// don't implement for now
}

static void vulkan_render_ellipse(struct wlr_renderer *renderer,
		const float (*color)[4], const float (*matrix)[16]) {
	// don't implement for now
}

static void vulkan_destroy(struct wlr_renderer *renderer) {
	// TODO
}

static struct wlr_renderer_impl renderer_impl = {
	.begin = vulkan_begin,
	.end = vulkan_end,
	.texture_create = vulkan_texture_create,
	.render_with_matrix = vulkan_render_surface,
	.render_quad = vulkan_render_quad,
	.render_ellipse = vulkan_render_ellipse,
	.destroy = vulkan_destroy
};

static bool init_instance(struct wlr_vulkan *vulkan, unsigned int ext_count,
		const char **exts) {
	uint32_t ecount = 0;
	VkResult ret = vkEnumerateInstanceExtensionProperties(NULL, &ecount, NULL);
	if ((ret != VK_SUCCESS) || (ecount == 0)) {
		wlr_log(L_ERROR, "Could not enumerate instance extensions:1");
		return false;
	}

	VkExtensionProperties *eprops = calloc(ext_count, sizeof(VkExtensionProperties));
	ret = vkEnumerateInstanceExtensionProperties(NULL, &ecount, eprops);
	if (ret != VK_SUCCESS) {
		wlr_log(L_ERROR, "Could not enumerate instance extensions:2");
		return false;
	}

	const char *extensions[ext_count + 1]; // TODO: really use stack?
	extensions[0] = VK_KHR_SURFACE_EXTENSION_NAME;
	memcpy(extensions + 1, exts, ext_count * sizeof(*exts));

	for(size_t i = 0; i < ext_count + 1; ++i) {
		bool found = false;
		for(size_t j = 0; j < ext_count; ++j) {
			if (strcmp(eprops[j].extensionName, extensions[i]) == 0) {
				found = true;
				break;
			}
		}

		if (!found) {
			wlr_log(L_ERROR, "Could not find extension %s", extensions[i]);
			return false;
		}
	}

	free(eprops);
	VkApplicationInfo application_info = {
		VK_STRUCTURE_TYPE_APPLICATION_INFO,
		NULL,
		"wlroots-compositor",
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
		ext_count + 1,
		extensions,
	};

	ret = vkCreateInstance(&instance_create_info, NULL, &vulkan->instance);
	if (ret != VK_SUCCESS) {
		wlr_log(L_ERROR, "Could not create instance: %d", ret);
		return false;
	}

	return true;
}

static bool init_device(struct wlr_vulkan *vulkan, unsigned int ext_count,
		const char **exts) {
	// TODO: choose the best physical device instead of the first
	uint32_t num_devs = 1;
	VkResult ret = vkEnumeratePhysicalDevices(vulkan->instance, &num_devs, 
		&vulkan->phdev);
	if (ret != VK_SUCCESS || !vulkan->phdev) {
		wlr_log(L_ERROR, "Could not retrieve physical device");
		return false;
	}

	// TODO: query them correctly, try to find one queue fam for both
	vulkan->graphics_queue_fam = 0;
	vulkan->present_queue_fam = 0;
	bool one_queue = (vulkan->graphics_queue_fam == vulkan->present_queue_fam);

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
	present_queue_info.queueFamilyIndex = vulkan->present_queue_fam;

	// TODO: correct queue creation (find one or two for graphics and presenting)
	VkDeviceQueueCreateInfo queue_infos[2] = {graphics_queue_info, present_queue_info};

	const char *extensions[ext_count + 3];
	extensions[0] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	extensions[1] = VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;
	extensions[2] = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
	memcpy(extensions + 3, exts, ext_count * sizeof(*exts));

	const char *layer_name = "VK_LAYER_LUNARG_standard_validation";

	VkDeviceCreateInfo device_create_info = {
		VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,  // sType
		NULL, // pNext
		0, // flags
		one_queue ? 1 : 2, // queueCreateInfoCount
		&queue_infos, // pQueueCreateInfos
		1, // enabledLayerCount
		&layer_name, // ppEnabledLayerNames
		ext_count + 3, // enabledExtensionCount
		extensions, // ppEnabledExtensionNames
		NULL // pEnabledFeatures
	};

	ret = vkCreateDevice(vulkan->phdev, &device_create_info, NULL, &vulkan->dev);
	if (ret != VK_SUCCESS){
		wlr_log(L_ERROR, "Failed to create vulkan device");
		return false;
	}

	vkGetDeviceQueue(vulkan->dev, vulkan->graphics_queue_fam, 0, &vulkan->graphics_queue);
	if (one_queue) {
		vulkan->present_queue = vulkan->graphics_queue;
	} else {
		vkGetDeviceQueue(vulkan->dev, vulkan->present_queue_fam, 0, &vulkan->present_queue);
	}

	return true;
}

struct wlr_swapchain *wlr_swapchain_init(struct wlr_vulkan *vulkan,
		VkSurfaceKHR surface) {
	struct wlr_swapchain *swapchain;
	if(!(swapchain = calloc(1, sizeof(*swapchain)))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_swapchain");
		return NULL;
	}
	swapchain->vulkan = vulkan;

	// TODO
	VkSwapchainKHR old = VK_NULL_HANDLE;
	int width = 800;
	int height = 500;
	bool vsync = true;
	VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
	VkColorSpaceKHR color_space;

	// NOTE: maybe don't use the prototypes
/*
	// PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR;
	// PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilitiesKHR;
	// PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR;
	// PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModesKHR;
	// PFN_vkCreateSwapchainKHR createSwapchainKHR;
	// PFN_vkDestroySwapchainKHR destroySwapchainKHR;
	// PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR;
	// PFN_vkAcquireNextImageKHR acquireNextImageKHR;
	// PFN_vkQueuePresentKHR queuePresentKHR;

	WLR_VK_PROC_INI(vulkan->instance, GetPhysicalDeviceSurfaceSupportKHR);
	WLR_VK_PROC_INI(vulkan->instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
	WLR_VK_PROC_INI(vulkan->instance, GetPhysicalDeviceSurfaceFormatsKHR);
	WLR_VK_PROC_INI(vulkan->instance, GetPhysicalDeviceSurfaceFormatsKHR);

	WLR_VK_PROC_DEV(vulkan->dev, CreateSwapchainKHR);
	WLR_VK_PROC_DEV(vulkan->dev, GetSwapchainImagesKHR);
*/

	VkSwapchainCreateInfoKHR info;
	memset(&info, 0, sizeof(info));

	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.pNext = NULL;
	info.surface = surface;
	info.oldSwapchain = old;
	info.imageFormat = format;
	info.imageColorSpace = color_space;

	// TODO: error checking
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan->phdev, surface, &caps);

	// Get available present modes
	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, NULL);

	VkPresentModeKHR *present_modes =
		calloc(present_mode_count, sizeof(VkPresentModeKHR));
	vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan->phdev, surface,
		&present_mode_count, present_modes);

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
	vkGetPhysicalDeviceFormatProperties(vulkan->phdev, format, &format_props);
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

	// TODO: make sure it's freed on errors below
	VkResult res = vkCreateSwapchainKHR(vulkan->dev, &info, NULL, &swapchain->swapchain);
	if (res != VK_SUCCESS || !swapchain->swapchain) {
		wlr_log(L_ERROR, "Failed to create vk swapchain");
		return NULL;
	}

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	/*
	if (oldSwapchain != VK_NULL_HANDLE)
	{
		for (uint32_t i = 0; i < imageCount; i++) {
			vkDestroyImageView(device, buffers[i].view, nullptr);
		}
		fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
	}
	*/

	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain, 
		&swapchain->image_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to get swapchain images:1");
		return NULL;
	}

	VkImage images[swapchain->image_count];

	res = vkGetSwapchainImagesKHR(vulkan->dev, swapchain->swapchain, 
		&swapchain->image_count, images);
	if (res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to get swapchain images:2");
		return NULL;
	}


	if(!(swapchain->buffers = calloc(swapchain->image_count, sizeof(*swapchain->buffers)))) {
		wlr_log(L_ERROR, "Failed to allocate swapchain buffers");
		return NULL;
	}

	for (uint32_t i = 0; i < swapchain->image_count; i++) {
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.pNext = NULL;
		viewInfo.format = format;
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.flags = 0;
		viewInfo.image = images[i];

		swapchain->buffers[i].image = images[i];
		res = vkCreateImageView(vulkan->dev, &viewInfo, NULL, 
			&swapchain->buffers[i].image_view);
		if(res != VK_SUCCESS) {
			// TODO: free resources
			wlr_log(L_ERROR, "Failed to create %dth image view for swapchain", i);
			return NULL;
		}
	}

	return swapchain;
}


struct wlr_vulkan *wlr_vulkan_create(
		unsigned int ini_ext_count, const char **ini_exts,
		unsigned int dev_ext_count, const char **dev_exts,
		bool debug) {
	struct wlr_vulkan *vulkan;
	if (!(vulkan = calloc(1, sizeof(*vulkan)))) {
		wlr_log_errno(L_ERROR, "failed to allocate wlr_vulkan");
		return NULL;
	}

	// TODO: use debug

	if (!init_instance(vulkan, ini_ext_count, ini_exts)) {
		wlr_log(L_ERROR, "Could not init vulkan instance");
		goto error;
	}

	if (!init_device(vulkan, dev_ext_count, dev_exts)) {
		wlr_log(L_ERROR, "Could not init vulkan device");
		goto error;
	}

	return vulkan;

error:
	// TODO: destruct already created vulkan resources
	free(vulkan);
	return NULL;
}

static bool init_pipeline(struct wlr_vulkan_renderer *renderer) {
	// util
	VkDevice dev = renderer->vulkan->dev;
	VkResult res;

	// TODO: formats and stuff
	// renderpass
	VkAttachmentDescription attachment = {
		0, // flags
		VK_FORMAT_B8G8R8A8_UNORM, // format
		VK_SAMPLE_COUNT_1_BIT, // samples
		VK_ATTACHMENT_LOAD_OP_CLEAR, // loadOp
		VK_ATTACHMENT_STORE_OP_STORE, // storeOp
		VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencil load
		VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencil store
		VK_IMAGE_LAYOUT_UNDEFINED, // initial layout
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, // final layout
	};

	VkAttachmentReference color_ref = {
		0, // attachment
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout
	};

	VkSubpassDescription subpass = {
		0, // flags
		VK_PIPELINE_BIND_POINT_GRAPHICS, // bind pint
		0, // input attachments
		NULL,
		1, // color attachments
		&color_ref,
		NULL, // resolve attachment
		NULL, // depth attachment
		0, // preserve attachments
		NULL
	};

	VkRenderPassCreateInfo rp_info = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		NULL, // pnext
		0, // flags
		1, // attachment count
		&attachment, // pAttachments
		1, // subpassCount
		&subpass, // pSubpasses
		0, // dependencies
		NULL
	};

	res = vkCreateRenderPass(dev, &rp_info, NULL, &renderer->render_pass);
	if (res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create render pass: %d", res);
		goto error;
	}

	// sampler
	VkSamplerCreateInfo sampler_info = {
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_FILTER_LINEAR, // magFilter
		VK_FILTER_LINEAR, // minFilter
		VK_SAMPLER_MIPMAP_MODE_NEAREST, // mipmapMode
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressu
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressv
		VK_SAMPLER_ADDRESS_MODE_REPEAT, // addressv
		0, // lodBias
		false, // anisotropy
		1.0, // maxAnisotrpy
		false, // compareEnable
		VK_COMPARE_OP_ALWAYS, // compareOp
		0, // minLod
		0.25, // maxLod
		VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, // borderColor
		false, // unnormalizedCoordinates
	};

	res = vkCreateSampler(dev, &sampler_info, NULL, &renderer->sampler);
	if (res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create sampler: %d", res);
		goto error;
	}

	// layouts
	// descriptor set
	VkDescriptorSetLayoutBinding ds_bindings[2] = {{
		0, // binding
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // type
		1, // count
		VK_SHADER_STAGE_VERTEX_BIT, // stage flags
		NULL,
	}, {
		1, // binding
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // type
		1, // count
		VK_SHADER_STAGE_FRAGMENT_BIT, // stage flags
		&renderer->sampler // immutable sampler
	}};

	VkDescriptorSetLayoutCreateInfo ds_info = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		NULL, // pnext
		0, // flags
		2, // binding count
		ds_bindings // bindings
	};

	res = vkCreateDescriptorSetLayout(dev, &ds_info, NULL, 
		&renderer->descriptor_set_layout);
	if(res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create descriptor set layout: %d", res);
		goto error;
	}

	// pipeline
	VkPipelineLayoutCreateInfo pl_info = {
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		NULL, // pnext
		0, // flags
		1, // setLayoutCount
		&renderer->descriptor_set_layout, // setLayouts
		0, // pushRangesCount
		NULL // pushRanges
	};

	res = vkCreatePipelineLayout(dev, &pl_info, NULL, &renderer->pipeline_layout);
	if(res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create pipeline layout: %d", res);
		goto error;
	}

	// pipeline
	// shader
	// frag
	size_t frag_size = sizeof(texture_frag_spv_data) / sizeof(texture_frag_spv_data[0]);
	VkShaderModuleCreateInfo frag_info = {
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		frag_size, // codeSize
		texture_frag_spv_data // pCode
	};

	VkShaderModule frag_module;
	res = vkCreateShaderModule(dev, &frag_info, NULL, &frag_module);
	if(res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create fragment shader module: %d", res);
		goto error;
	}

	// vert
	size_t vert_size = sizeof(texture_vert_spv_data) / sizeof(texture_vert_spv_data[0]);
	VkShaderModuleCreateInfo vert_info = {
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		vert_size, // codeSize
		texture_vert_spv_data // pCode
	};

	VkShaderModule vert_module;
	res = vkCreateShaderModule(dev, &vert_info, NULL, &vert_module);
	if(res != VK_SUCCESS) {
		wlr_log(L_ERROR, "Failed to create vertex shader module: %d", res);
		goto error;
	}

	VkPipelineShaderStageCreateInfo stages[2] = {{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_SHADER_STAGE_VERTEX_BIT, // stage
		vert_module, // module
		"main", // name
		NULL // specialization info
	}, {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		NULL, // pnext
		0, // flags
		VK_SHADER_STAGE_FRAGMENT_BIT, // stage
		frag_module, // module
		"main", // name
		NULL // specialization info
	}};

	// info
	VkPipelineInputAssemblyStateCreateInfo input_assembly;
	VkPipelineRasterizationStateCreateInfo rasterization;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineDynamicStateCreateInfo dynamic;

	VkGraphicsPipelineCreateInfo pinfo;
	pinfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pinfo.pNext = NULL;
	pinfo.flags = 0;
	pinfo.layout = renderer->pipeline_layout;
	pinfo.renderPass = renderer->render_pass;
	pinfo.subpass = 0;
	pinfo.stageCount = 2;
	pinfo.pStages = stages;

	pinfo.pInputAssemblyState = &input_assembly;
	pinfo.pRasterizationState = &rasterization;
	pinfo.pColorBlendState = &blend;
	pinfo.pMultisampleState = &multisample;
	pinfo.pViewportState = &viewport;
	pinfo.pDynamicState = &dynamic;
	pinfo.pDepthStencilState = NULL;
	pinfo.pTessellationState = NULL;
	pinfo.pVertexInputState = NULL;

	return true;

error:
	// TODO: cleanup
	return false;
}


struct wlr_renderer *wlr_vulkan_renderer_create(struct wlr_vulkan *vulkan) {
	struct wlr_vulkan_renderer *renderer;
	if (!(renderer = calloc(1, sizeof(*renderer)))) {
		wlr_log_errno(L_ERROR, "failed to allocate wlr_vulkan_renderer");
		return NULL;
	}

	// TODO: correctly cleanup on error
	if (!init_pipeline(renderer)) {
		wlr_log(L_ERROR, "Could not init vulkan pipeline");
		return NULL;
	}

	renderer->vulkan = vulkan;
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	return &renderer->wlr_renderer;
}
