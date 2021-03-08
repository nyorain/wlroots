#include <drm_fourcc.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

// reversed endianess of shm and vulkan format names
static const struct wlr_vk_format formats[] = {
	{
		.drm_format = DRM_FORMAT_ARGB8888,
		.vk_format = VK_FORMAT_B8G8R8A8_SRGB,
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
		.drm_format = DRM_FORMAT_XRGB8888,
		.vk_format = VK_FORMAT_B8G8R8A8_SRGB,
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
		.drm_format = DRM_FORMAT_XBGR8888,
		.vk_format = VK_FORMAT_R8G8B8A8_SRGB,
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
		.drm_format = DRM_FORMAT_ABGR8888,
		.vk_format = VK_FORMAT_R8G8B8A8_SRGB,
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
	// TODO: these should be srgb as well. We might have
	// to transform them in shader, manually (not done at the moment)
	{
		.drm_format = DRM_FORMAT_YUYV,
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
		.drm_format = DRM_FORMAT_UYVY,
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
		.drm_format = DRM_FORMAT_NV12,
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
		.drm_format = DRM_FORMAT_NV16,
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
		.drm_format = DRM_FORMAT_YUV420,
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
		.drm_format = DRM_FORMAT_YUV422,
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
		.drm_format = DRM_FORMAT_YUV444,
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

const struct wlr_vk_format *vulkan_get_format_from_drm(uint32_t drm_format) {
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
		if (formats[i].drm_format == drm_format) {
			return &formats[i];
		}
	}
	return NULL;
}

void wlr_vk_format_props_query(struct wlr_vk_device *dev,
		const struct wlr_vk_format* format) {

	if (!dev->features.ycbcr && format->ycbcr) {
		return;
	}

	wlr_log(WLR_INFO, "vulkan: Checking support for format %.4s (0x%" PRIx32 ")",
		(const char*) &format->drm_format, format->drm_format);
	VkResult res;

	const VkImageUsageFlags render_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	const VkImageUsageFlags tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags dma_tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT;

	const VkFormatFeatureFlags tex_features = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	const VkFormatFeatureFlags render_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	const VkFormatFeatureFlags dma_tex_features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	// get general features and modifiers
	VkFormatProperties2 fmtp = {0};
	fmtp.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;

	VkDrmFormatModifierPropertiesListEXT modp = {0};
	modp.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
	fmtp.pNext = &modp;

	dev->instance->api.getPhysicalDeviceFormatProperties2(dev->phdev,
		format->vk_format, &fmtp);

	// detailed check
	VkPhysicalDeviceImageFormatInfo2 fmti = {0};
	fmti.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
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

	bool add_fmt_props = false;
	struct wlr_vk_format_props props = {0};
	props.format = *format;

	wlr_log(WLR_INFO, "  drmFormatModifierCount: %d", modp.drmFormatModifierCount);
	if (modp.drmFormatModifierCount > 0) {
		// the first call to vkGetPhysicalDeviceFormatProperties2 did only
		// retrieve the number of modifiers, we now have to retrieve
		// the modifiers
		modp.pDrmFormatModifierProperties = calloc(modp.drmFormatModifierCount,
			sizeof(*modp.pDrmFormatModifierProperties));
		if (!modp.pDrmFormatModifierProperties) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return;
		}

		dev->instance->api.getPhysicalDeviceFormatProperties2(dev->phdev,
			format->vk_format, &fmtp);

		props.render_mods = calloc(modp.drmFormatModifierCount,
			sizeof(*props.render_mods));
		if (!props.render_mods) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			free(modp.pDrmFormatModifierProperties);
			return;
		}

		props.texture_mods = calloc(modp.drmFormatModifierCount,
			sizeof(*props.texture_mods));
		if (!props.texture_mods) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			free(modp.pDrmFormatModifierProperties);
			free(props.render_mods);
			return;
		}

		// detailed check
		// format info
		fmti.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
		fmti.pNext = &efmti;

		VkPhysicalDeviceImageDrmFormatModifierInfoEXT modi = {0};
		modi.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
		modi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		efmti.pNext = &modi;

		// format properties
		ifmtp.pNext = &efmtp;
		const VkExternalMemoryProperties *emp = &efmtp.externalMemoryProperties;

		for (unsigned i = 0u; i < modp.drmFormatModifierCount; ++i) {
			VkDrmFormatModifierPropertiesEXT m =
				modp.pDrmFormatModifierProperties[i];
			wlr_log(WLR_INFO, "  modifier: 0x%"PRIx64 ": features 0x%"PRIx32,
				m.drmFormatModifier, m.drmFormatModifierTilingFeatures);

			// check that specific modifier for render usage
			if (m.drmFormatModifierTilingFeatures & render_features) {
				fmti.usage = render_usage;

				modi.drmFormatModifier = m.drmFormatModifier;
				res = dev->instance->api.getPhysicalDeviceImageFormatProperties2(
					dev->phdev, &fmti, &ifmtp);
				if (res != VK_SUCCESS) {
					if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
						wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
							res);
					}

					wlr_log(WLR_INFO, "    >> rendering: format not supported");
				} else if ((emp->externalMemoryFeatures &
						VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
					unsigned c = props.render_mod_count;
					VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
					VkExternalMemoryProperties emp = efmtp.externalMemoryProperties;
					props.render_mods[c].props = m;
					props.render_mods[c].max_extent.width = me.width;
					props.render_mods[c].max_extent.height = me.height;
					props.render_mods[c].dmabuf_flags = emp.externalMemoryFeatures;
					props.render_mods[c].export_imported =
						(emp.exportFromImportedHandleTypes &
						 VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
					++props.render_mod_count;

					add_fmt_props = true;
					wlr_drm_format_set_add(&dev->dmabuf_render_formats,
						format->drm_format, m.drmFormatModifier);

					wlr_log(WLR_INFO, "    >> rendering: supported");
				} else {
					wlr_log(WLR_INFO, "    >> rendering: importing not supported");
				}
			} else {
				wlr_log(WLR_INFO, "    >> rendering: format features not supported");
			}

			// check that specific modifier for texture usage
			if (m.drmFormatModifierTilingFeatures & dma_tex_features) {
				fmti.usage = dma_tex_usage;

				modi.drmFormatModifier = m.drmFormatModifier;
				res = dev->instance->api.getPhysicalDeviceImageFormatProperties2(
					dev->phdev, &fmti, &ifmtp);
				if (res != VK_SUCCESS) {
					if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
						wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
							res);
					}

					wlr_log(WLR_INFO, "    >> dmatex: format not supported");
				} else if ((emp->externalMemoryFeatures &
						VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
					unsigned c = props.texture_mod_count;
					VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
					VkExternalMemoryProperties emp = efmtp.externalMemoryProperties;
					props.texture_mods[c].props = m;
					props.texture_mods[c].max_extent.width = me.width;
					props.texture_mods[c].max_extent.height = me.height;
					props.texture_mods[c].dmabuf_flags = emp.externalMemoryFeatures;
					props.texture_mods[c].export_imported =
						(emp.exportFromImportedHandleTypes &
						 VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);
					++props.texture_mod_count;

					add_fmt_props = true;
					wlr_drm_format_set_add(&dev->dmabuf_texture_formats,
						format->drm_format, m.drmFormatModifier);

					wlr_log(WLR_INFO, "    >> dmatex: supported");
				} else {
					wlr_log(WLR_INFO, "    >> dmatex: importing not supported");
				}
			} else {
				wlr_log(WLR_INFO, "    >> dmatex: format features not supported");
			}
		}

		free(modp.pDrmFormatModifierProperties);
	}

	// non-dmabuf texture properties
	if (fmtp.formatProperties.optimalTilingFeatures & tex_features) {
		fmti.pNext = NULL;
		ifmtp.pNext = NULL;
		fmti.tiling = VK_IMAGE_TILING_OPTIMAL;
		fmti.usage = tex_usage;

		res = dev->instance->api.getPhysicalDeviceImageFormatProperties2(
			dev->phdev, &fmti, &ifmtp);
		if (res != VK_SUCCESS) {
			if (res != VK_ERROR_FORMAT_NOT_SUPPORTED) {
				wlr_vk_error("vkGetPhysicalDeviceImageFormatProperties2",
					res);
			}

			wlr_log(WLR_INFO, " >> shmtex: format not supported");
		} else {
			VkExtent3D me = ifmtp.imageFormatProperties.maxExtent;
			props.max_extent.width = me.width;
			props.max_extent.height = me.height;
			props.features = fmtp.formatProperties.optimalTilingFeatures;

			wlr_log(WLR_INFO, " >> shmtex: supported");

			dev->shm_formats[dev->shm_format_count] = format->drm_format;
			++dev->shm_format_count;

			add_fmt_props = true;
		}
	} else {
		wlr_log(WLR_INFO, " >> shmtex: format features not supported");
	}

	if (add_fmt_props) {
		dev->format_props[dev->format_prop_count] = props;
		++dev->format_prop_count;
	}
}

void wlr_vk_format_props_finish(struct wlr_vk_format_props *props) {
	free(props->texture_mods);
	free(props->render_mods);
}

struct wlr_vk_format_modifier_props *wlr_vk_format_props_find_modifier(
		struct wlr_vk_format_props *props, uint64_t mod, bool render) {
	if (render) {
		for (unsigned i = 0u; i < props->render_mod_count; ++i) {
			if (props->render_mods[i].props.drmFormatModifier == mod) {
				return &props->render_mods[i];
			}
		}
	} else {
		for (unsigned i = 0u; i < props->texture_mod_count; ++i) {
			if (props->texture_mods[i].props.drmFormatModifier == mod) {
				return &props->texture_mods[i];
			}
		}
	}

	return NULL;
}

