#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <render/vulkan.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include <wlr/config.h>
#include <vulkan/vulkan.h>

// Returns the name of the first extension that could not be found or NULL.
static const char *find_extensions(const VkExtensionProperties *avail,
		unsigned availc, const char **req, unsigned reqc) {
	// check if all required extensions are supported
	for (size_t i = 0; i < reqc; ++i) {
		bool found = false;
		for (size_t j = 0; j < availc; ++j) {
			if (!strcmp(avail[j].extensionName, req[i])) {
				found = true;
				break;
			}
		}

		if (!found) {
			return req[i];
		}
	}

	return NULL;
}

static VkBool32 debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT *debug_data,
		void *data) {

	((void) data);

	// we ignore some of the non-helpful warnings
	/*
	static const char *const ignored[] = {
		// TODO: any of these needed?

		// if clear is used before any other command
		// "UNASSIGNED-CoreValidation-DrawState-ClearCmdBeforeDraw",
		// notifies us that shader output is not consumed since
		// we use the shared vertex buffer with uv output
		// "UNASSIGNED-CoreValidation-Shader-OutputNotConsumed",
		// always warns for queue_external/foreign transitions
		// fixed in https://github.com/KhronosGroup/Vulkan-ValidationLayers/pull/311,
		// no release with that in
		// "UNASSIGNED-VkImageMemoryBarrier-image-00004",
		// pretty sure this is a bug, nagging us about external image
		// realease/acquire. TODO: investigate/report upstream
		// "UNASSIGNED-VkImageMemoryBarrier-image-00003",
	};

	if (debug_data->pMessageIdName) {
		for (unsigned i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i) {
			if (!strcmp(debug_data->pMessageIdName, ignored[i])) {
				return false;
			}
		}
	}
	*/

	enum wlr_log_importance importance;
	switch(severity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
			importance = WLR_ERROR;
			break;
		default:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			importance = WLR_INFO;
			break;
	}

	wlr_log(importance, "%s (%s)", debug_data->pMessage,
		debug_data->pMessageIdName);
	if (debug_data->queueLabelCount > 0) {
		const char *name = debug_data->pQueueLabels[0].pLabelName;
		if (name) {
			wlr_log(importance, "    last label '%s'", name);
		}
	}

	for (unsigned i = 0; i < debug_data->objectCount; ++i) {
		if (debug_data->pObjects[i].pObjectName) {
			wlr_log(importance, "    involving '%s'", debug_data->pMessage);
		}
	}

	return false;
}


// instance
struct wlr_vk_instance *wlr_vk_instance_create(
		unsigned ext_count, const char **exts, bool debug,
		const char *compositor_name, unsigned compositor_version) {

	// query extension support
	uint32_t avail_extc = 0;
	VkResult res;
	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vk_error("Could not enumerate instance extensions (1)", res);
		return NULL;
	}

	VkExtensionProperties avail_ext_props[avail_extc + 1];
	res = vkEnumerateInstanceExtensionProperties(NULL, &avail_extc,
		avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not enumerate instance extensions (2)", res);
		return NULL;
	}

	for (size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Instance extensions %s",
			avail_ext_props[j].extensionName);
	}

	// create instance
	struct wlr_vk_instance *ini = calloc(1, sizeof(*ini));
	if (!ini) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		return NULL;
	}

	bool debug_utils_found = false;
	ini->extensions = calloc(1 + ext_count, sizeof(*ini->extensions));
	if (!ini->extensions) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	// find extensions
	for (unsigned i = 0; i < ext_count; ++i) {
		if (find_extensions(avail_ext_props, avail_extc, &exts[i], 1)) {
			wlr_log(WLR_DEBUG, "vulkan instance extension %s not found",
				exts[i]);
			continue;
		}

		ini->extensions[ini->extension_count++] = exts[i];
	}

	if (debug) {
		const char *name = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			debug_utils_found = true;
			ini->extensions[ini->extension_count++] = name;
		}
	}

	VkApplicationInfo application_info = {0};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = compositor_name;
	application_info.applicationVersion = compositor_version;
	application_info.pEngineName = "wlroots";
	application_info.engineVersion = WLR_VERSION_NUM;
	application_info.apiVersion = VK_MAKE_VERSION(1,1,0);

	// standard_validation: reports error in api usage to debug callback
	// renderdoc: allows to capture (and debug) frames with renderdoc
	//   renderdoc has problems with some extensions we use atm so
	//   does not work
	const char *layers[] = {
		"VK_LAYER_KHRONOS_validation",
		// "VK_LAYER_RENDERDOC_Capture",
		// "VK_LAYER_live_introspection",
	};

	unsigned layer_count = debug * (sizeof(layers) / sizeof(layers[0]));

	VkInstanceCreateInfo instance_info = {0};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_info.pApplicationInfo = &application_info;
	instance_info.enabledExtensionCount = ini->extension_count;
	instance_info.ppEnabledExtensionNames = ini->extensions;
	instance_info.enabledLayerCount = layer_count;
	instance_info.ppEnabledLayerNames = layers;

	VkDebugUtilsMessageSeverityFlagsEXT severity =
		// VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	VkDebugUtilsMessageTypeFlagsEXT types =
		// VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

	VkDebugUtilsMessengerCreateInfoEXT debug_info = {0};
	debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debug_info.messageSeverity = severity;
	debug_info.messageType = types;
	debug_info.pfnUserCallback = &debug_callback;
	debug_info.pUserData = ini;

	if (debug_utils_found) {
		// already adding the debug utils messenger extension to
		// instance creation gives us additional information during
		// instance creation and destruction, can be useful for debugging
		// layers/extensions not being found.
		instance_info.pNext = &debug_info;
	}

	res = vkCreateInstance(&instance_info, NULL, &ini->instance);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not create instance", res);
		goto error;
	}

	// debug callback
	if (debug_utils_found) {
		ini->api.createDebugUtilsMessengerEXT =
			(PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					ini->instance, "vkCreateDebugUtilsMessengerEXT");
		ini->api.destroyDebugUtilsMessengerEXT =
			(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
					ini->instance, "vkDestroyDebugUtilsMessengerEXT");

		if (ini->api.createDebugUtilsMessengerEXT) {
			ini->api.createDebugUtilsMessengerEXT(ini->instance,
				&debug_info, NULL, &ini->messenger);
		} else {
			wlr_log(WLR_ERROR, "vkCreateDebugUtilsMessengerEXT not found");
		}
	}

	return ini;

error:
	wlr_vk_instance_destroy(ini);
	return NULL;
}

void wlr_vk_instance_destroy(struct wlr_vk_instance *ini) {
	if (!ini) {
		return;
	}

	if (ini->messenger && ini->api.destroyDebugUtilsMessengerEXT) {
		ini->api.destroyDebugUtilsMessengerEXT(ini->instance,
			ini->messenger, NULL);
	}

	if (ini->instance) {
		vkDestroyInstance(ini->instance, NULL);
	}

	free(ini->extensions);
	free(ini);
}

// device
struct wlr_vk_device *wlr_vk_device_create(struct wlr_vk_instance *ini,
		VkPhysicalDevice phdev, unsigned ext_count, const char **exts) {
	VkResult res;
	const char *name;

	// check for extensions
	uint32_t avail_extc = 0;
	res = vkEnumerateDeviceExtensionProperties(phdev, NULL,
		&avail_extc, NULL);
	if ((res != VK_SUCCESS) || (avail_extc == 0)) {
		wlr_vk_error("Could not enumerate device extensions (1)", res);
		return NULL;
	}

	VkExtensionProperties avail_ext_props[avail_extc + 1];
	res = vkEnumerateDeviceExtensionProperties(phdev, NULL,
		&avail_extc, avail_ext_props);
	if (res != VK_SUCCESS) {
		wlr_vk_error("Could not enumerate device extensions (2)", res);
		return NULL;
	}

	for (size_t j = 0; j < avail_extc; ++j) {
		wlr_log(WLR_INFO, "Vulkan Device extension %s",
			avail_ext_props[j].extensionName);
	}

	// create device
	struct wlr_vk_device *dev = calloc(1, sizeof(*dev));
	if (!dev) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		return NULL;
	}

	dev->phdev = phdev;
	dev->instance = ini;
	dev->extensions = calloc(7 + ext_count, sizeof(*ini->extensions));
	if (!dev->extensions) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	// find extensions
	for (unsigned i = 0; i < ext_count; ++i) {
		if (find_extensions(avail_ext_props, avail_extc, &exts[i], 1)) {
			wlr_log(WLR_DEBUG, "vulkan device extension %s not found",
				exts[i]);
			continue;
		}

		dev->extensions[dev->extension_count++] = exts[i];
	}

	name = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		dev->extensions[dev->extension_count++] = name;

		name = VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			dev->extensions[dev->extension_count++] = name;
		}
	}

	// for dmabuf/drm importing we require at least the
	// 'external_memory_fd, external_memory_dma_buf, queue_family_foreign'
	// extensions. So only enable them if all three are available (assumption
	// throughout the codebase).
	const char *names[] = {
		VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
		VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
	};

	unsigned nc = sizeof(names) / sizeof(names[0]);
	bool has_dmabuf = false;
	if (find_extensions(avail_ext_props, avail_extc, names, nc) == NULL) {
		has_dmabuf = true;
		for (unsigned i = 0u; i < nc; ++i) {
			dev->extensions[dev->extension_count++] = names[i];
		}

		// doesn't strictly depend on any of the two but we have no
		// use for this without the dma_buf extension
		name = VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME;
		if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
			dev->extensions[dev->extension_count++] = name;
		}
	} else {
		wlr_log(WLR_ERROR, "vulkan: required dmabuf extensions not found");
		goto error;
	}

	name = VK_KHR_INCREMENTAL_PRESENT_EXTENSION_NAME;
	if (find_extensions(avail_ext_props, avail_extc, &name, 1) == NULL) {
		dev->extensions[dev->extension_count++] = name;
	}

	// check/enable features
	VkPhysicalDeviceFeatures2 features = {0};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

	VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr_features = {0};
	ycbcr_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES;
	features.pNext = &ycbcr_features;

	vkGetPhysicalDeviceFeatures2(phdev, &features);
	dev->features.ycbcr = ycbcr_features.samplerYcbcrConversion;
	wlr_log(WLR_INFO, "YCbCr device feature: %d", dev->features.ycbcr);

	// queue families
	{
		uint32_t qfam_count;
		vkGetPhysicalDeviceQueueFamilyProperties(phdev, &qfam_count, NULL);
		assert(qfam_count > 0);
		VkQueueFamilyProperties queue_props[qfam_count];
		vkGetPhysicalDeviceQueueFamilyProperties(phdev, &qfam_count,
			queue_props);

		bool graphics_found = false;
		for (unsigned i = 0u; i < qfam_count; ++i) {
			graphics_found = queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT;
			if (graphics_found) {
				dev->queue_family = i;
				break;
			}
		}

		assert(graphics_found);
	}

	const float prio = 1.f;
	VkDeviceQueueCreateInfo qinfo = {};
	qinfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qinfo.queueFamilyIndex = dev->queue_family;
	qinfo.queueCount = 1;
	qinfo.pQueuePriorities = &prio;

	VkDeviceCreateInfo dev_info = {0};
	dev_info.pNext = &ycbcr_features;
	dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dev_info.queueCreateInfoCount = 1u;
	dev_info.pQueueCreateInfos = &qinfo;
	dev_info.enabledExtensionCount = dev->extension_count;
	dev_info.ppEnabledExtensionNames = dev->extensions;

	res = vkCreateDevice(phdev, &dev_info, NULL, &dev->dev);
	if (res != VK_SUCCESS){
		wlr_vk_error("Failed to create vulkan device", res);
		goto error;
	}

	vkGetDeviceQueue(dev->dev, dev->queue_family, 0, &dev->queue);

	// load api
	if (has_dmabuf) {
		dev->api.getMemoryFdPropertiesKHR =
			(PFN_vkGetMemoryFdPropertiesKHR) vkGetDeviceProcAddr(
					dev->dev, "vkGetMemoryFdPropertiesKHR");
		if (!dev->api.getMemoryFdPropertiesKHR) {
			wlr_log(WLR_ERROR, "Failed to retrieve required dev func pointers");
			goto error;
		}
	}

	// - check device format support -
	size_t max_fmts;
	const struct wlr_vk_format *fmts = vulkan_get_format_list(&max_fmts);
	dev->shm_formats = calloc(max_fmts, sizeof(*dev->shm_formats));
	dev->format_props = calloc(max_fmts, sizeof(*dev->format_props));
	if (!dev->shm_formats || !dev->format_props) {
		wlr_log_errno(WLR_ERROR, "allocation failed");
		goto error;
	}

	for (unsigned i = 0u; i < max_fmts; ++i) {
		wlr_vk_format_props_query(dev, &fmts[i]);
	}

	return dev;

error:
	wlr_vk_device_destroy(dev);
	return NULL;
}

void wlr_vk_device_destroy(struct wlr_vk_device *dev) {
	if (!dev) {
		return;
	}

	if (dev->dev) {
		vkDestroyDevice(dev->dev, NULL);
	}

	wlr_drm_format_set_finish(&dev->dmabuf_render_formats);
	wlr_drm_format_set_finish(&dev->dmabuf_texture_formats);

	for (unsigned i = 0u; i < dev->format_prop_count; ++i) {
		wlr_vk_format_props_finish(&dev->format_props[i]);
	}

	free(dev->extensions);
	free(dev->shm_formats);
	free(dev->format_props);
	free(dev);
}
