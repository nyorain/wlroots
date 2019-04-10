#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

// reversed endianess of shm and vulkan format names
static const struct wlr_vk_format formats[] = {
	{
		.wl_format = WL_SHM_FORMAT_ARGB8888,
		.vk_format = VK_FORMAT_B8G8R8A8_UNORM,
		.has_alpha = true,
		.ycbcr = false,
		.plane_count = 1,
		.planes = {{
			.bpb = 32,
			.hsub = 1,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_XRGB8888,
		.vk_format = VK_FORMAT_B8G8R8A8_UNORM,
		.has_alpha = false,
		.ycbcr = false,
		.plane_count = 1,
		.planes = {{
			.bpb = 32,
			.hsub = 1,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_XBGR8888,
		.vk_format = VK_FORMAT_R8G8B8A8_UNORM,
		.has_alpha = false,
		.ycbcr = false,
		.plane_count = 1,
		.planes = {{
			.bpb = 32,
			.hsub = 1,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_ABGR8888,
		.vk_format = VK_FORMAT_R8G8B8A8_UNORM,
		.has_alpha = true,
		.ycbcr = false,
		.plane_count = 1,
		.planes = {{
			.bpb = 32,
			.hsub = 1,
			.vsub = 1,
		}},
	},
	// TODO: not sure about mapping/byte order from here
	// switch first two?
	{
		.wl_format = WL_SHM_FORMAT_YUYV,
		.vk_format = VK_FORMAT_B8G8R8G8_422_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 1,
		.planes = {{
			.bpb = 32, // 2x1 block
			.hsub = 2,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_UYVY,
		.vk_format = VK_FORMAT_G8B8G8R8_422_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 1,
		.planes = {{
			.bpb = 32, // 2x1 block
			.hsub = 2,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_NV12,
		.vk_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 2,
		.planes = {{
			.bpb = 8, // Y
			.hsub = 1,
			.vsub = 1,
		}, {
			.bpb = 16, // CbCr, 2x2 subsampled
			.hsub = 2,
			.vsub = 2,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_NV16,
		.vk_format = VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 2,
		.planes = {{
			.bpb = 8, // Y
			.hsub = 1,
			.vsub = 1,
		}, {
			.bpb = 16, // CbCr, 2x1 subsampled
			.hsub = 2,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_YUV420,
		.vk_format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 3,
		.planes = {{
			.bpb = 8, // Y
			.vsub = 1,
			.hsub = 1,
		}, {
			.bpb = 8, // Cb, 2x2 subsampled
			.hsub = 2,
			.vsub = 2,
		}, {
			.bpb = 8, // Cr, 2x2 subsampled
			.hsub = 2,
			.vsub = 2,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_YUV422,
		.vk_format = VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 3,
		.planes = {{
			.bpb = 8, // Y
			.hsub = 1,
			.vsub = 1,
		}, {
			.bpb = 8, // Cb, 2x1 subsampled
			.hsub = 2,
			.vsub = 1,
		}, {
			.bpb = 8, // Cr, 2x1 subsampled
			.hsub = 2,
			.vsub = 1,
		}},
	},
	{
		.wl_format = WL_SHM_FORMAT_YUV444,
		.vk_format = VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,
		.has_alpha = false,
		.ycbcr = true,
		.plane_count = 3,
		.planes = {{
			.bpb = 8, // Y
			.hsub = 1,
			.vsub = 1,
		}, {
			.bpb = 8, // Cb
			.hsub = 1,
			.vsub = 1,
		}, {
			.bpb = 8, // Cr
			.hsub = 1,
			.vsub = 1,
		}},
	}
};

const struct wlr_vk_format *vulkan_get_format_list(size_t *len) {
	*len = sizeof(formats) / sizeof(formats[0]);
	return formats;
}

const struct wlr_vk_format *vulkan_get_format_from_wl(
		enum wl_shm_format fmt) {
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		if (formats[i].wl_format == fmt) {
			return &formats[i];
		}
	}
	return NULL;
}

bool wlr_vk_format_props_query(struct wlr_vk_device *dev,
		struct wlr_vk_format_props *props, VkImageUsageFlags usage,
		bool prefer_linear) {

	VkResult res;
	struct wlr_vk_format *format = &props->format;
	bool dma_ext = vulkan_has_extension(dev->extension_count, dev->extensions,
		VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
	bool drm_fmt_ext = dma_ext && vulkan_has_extension(
		dev->extension_count, dev->extensions,
		VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);

	VkFormatFeatureFlags req_features = 0;
	if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
		// we can't use it sampled (ie. with a sampler) if it requires ycbcr
		// support but that couldn't be enabled
		if (!dev->features.ycbcr && props->format.ycbcr) {
			return false;
		}
		req_features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	}
	if (usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
		req_features |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	}
	if (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
		req_features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
	}
	if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
		req_features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	}

	// get general features and modifiers
	VkFormatProperties2 fmtp = {0};
	fmtp.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;

	VkDrmFormatModifierPropertiesListEXT modp = {0};
	if (drm_fmt_ext) {
		modp.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
		fmtp.pNext = &modp;
	}

	vkGetPhysicalDeviceFormatProperties2(dev->phdev, props->format.vk_format,
		&fmtp);

	// detailed check
	VkPhysicalDeviceImageFormatInfo2 fmti = {0};
	fmti.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
	fmti.usage = usage;
	fmti.type = VK_IMAGE_TYPE_2D;
	fmti.format = format->vk_format;

	VkImageFormatProperties2 ifmtp = {0};
	ifmtp.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

	// only added if dmabuf/drm_fmt_ext supported
	VkPhysicalDeviceExternalImageFormatInfo efmti = {0};
	efmti.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
	efmti.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

	VkExternalImageFormatProperties efmtp = {0};
	efmtp.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;

	props->modifier_count = 0;
	uint32_t f = drm_from_shm_format(format->wl_format);
	wlr_log(WLR_INFO, "%d modifiers on format %.4s", (int) modp.drmFormatModifierCount,
		(const char*) &f);
	if (modp.drmFormatModifierCount > 0) {
		// the first call to vkGetPhysicalDeviceFormatProperties2 did only
		// retrieve the number of modifiers, we now have to retrieve
		// the modifiers
		modp.pDrmFormatModifierProperties = calloc(
			modp.drmFormatModifierCount,
			sizeof(*modp.pDrmFormatModifierProperties));
		if (!modp.pDrmFormatModifierProperties) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}

		vkGetPhysicalDeviceFormatProperties2(dev->phdev,
			props->format.vk_format, &fmtp);

		// detailed check
		// format info
		fmti.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
		fmti.pNext = &efmti;

		VkPhysicalDeviceImageDrmFormatModifierInfoEXT modi = {0};
		modi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
		efmti.pNext = &modi;

		// format properties
		ifmtp.pNext = &efmtp;

		props->modifiers = calloc(modp.drmFormatModifierCount,
			sizeof(*props->modifiers));
		if (!props->modifiers) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}

		for (unsigned i = 0u; i < modp.drmFormatModifierCount; ++i) {
			VkDrmFormatModifierPropertiesEXT m =
				modp.pDrmFormatModifierProperties[i];
			VkFormatFeatureFlags features = m.drmFormatModifierTilingFeatures;
			if ((features & req_features) != req_features) {
				continue;
			}

			modi.drmFormatModifier = m.drmFormatModifier;
			res = vkGetPhysicalDeviceImageFormatProperties2(dev->phdev,
				&fmti, &ifmtp);
			if (res != VK_SUCCESS) {
				if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
					wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
						res);
				}
				continue;
			}

			unsigned c = props->modifier_count;
			VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
			VkExternalMemoryProperties emp = efmtp.externalMemoryProperties;
			props->modifiers[c].props = m;
			props->modifiers[c].max_extent.width = me.width;
			props->modifiers[c].max_extent.height = me.height;
			props->modifiers[c].dmabuf_flags = emp.externalMemoryFeatures;
			props->modifiers[c].export_imported =
				(emp.exportFromImportedHandleTypes &
				 VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
			++props->modifier_count;

			wlr_log(WLR_INFO, "modifier %lu on format %.4s",
				m.drmFormatModifier, (const char*) &f);
		}

		free(modp.pDrmFormatModifierProperties);
	} else /* if (!drm_fmt_ext && dma_ext) */ {
		// in this case we can still fall back to importing images
		// without the extension; relying on DRM_FORMAT_MOD_INVALID.
		// technically not guaranteed to work where modifiers are used.
		// TODO: should probably be removed as fallback
		fmti.pNext = &efmti;
		ifmtp.pNext = &efmtp;
		res = vkGetPhysicalDeviceImageFormatProperties2(dev->phdev,
			&fmti, &ifmtp);

		if (res != VK_SUCCESS) {
			if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
				wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
					res);
			}
		} else {
			// TODO: linear?
			props->modifier_count = 2u;
			props->modifiers = calloc(props->modifier_count,
				sizeof(*props->modifiers));
			if (!props->modifiers) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				return false;
			}

			VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
			VkExternalMemoryProperties emp = efmtp.externalMemoryProperties;
			props->modifiers[0].props.drmFormatModifier = DRM_FORMAT_MOD_INVALID;
			props->modifiers[0].props.drmFormatModifierPlaneCount =
				props->format.plane_count;
			props->modifiers[0].props.drmFormatModifierTilingFeatures =
				req_features; // XXX
			props->modifiers[0].max_extent.width = me.width;
			props->modifiers[0].max_extent.height = me.height;
			props->modifiers[0].dmabuf_flags = emp.externalMemoryFeatures;
			props->modifiers[0].export_imported =
				(emp.exportFromImportedHandleTypes &
				 VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

			props->modifiers[1] = props->modifiers[0];
			props->modifiers[1].props.drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
		}
	}

	// non-dmabuf properties
	fmti.pNext = NULL;
	ifmtp.pNext = NULL;
	if (prefer_linear) {
		props->features = fmtp.formatProperties.linearTilingFeatures;
		fmti.tiling = VK_IMAGE_TILING_LINEAR;
	} else {
		props->features = fmtp.formatProperties.optimalTilingFeatures;
		fmti.tiling = VK_IMAGE_TILING_OPTIMAL;
	}

	if ((props->features & req_features) != req_features) {
		return false;
	}

	res = vkGetPhysicalDeviceImageFormatProperties2(dev->phdev,
		&fmti, &ifmtp);
	if (res != VK_SUCCESS) {
		if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
			wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
				res);
		}
		return false;
	}

	VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
	props->max_extent.width = me.width;
	props->max_extent.height = me.height;
	return true;
}

void wlr_vk_format_props_finish(struct wlr_vk_format_props *props) {
	free(props->modifiers);
}

struct wlr_vk_format_modifier_props *wlr_vk_format_props_find_modifier(
		struct wlr_vk_format_props *props, uint64_t mod) {
	for (unsigned i = 0u; i < props->modifier_count; ++i) {
		if (props->modifiers[i].props.drmFormatModifier == mod) {
			return &props->modifiers[i];
		}
	}

	return NULL;
}
