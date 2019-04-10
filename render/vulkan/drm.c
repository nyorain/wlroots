#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <vulkan/vulkan.h>
#include <render/vulkan.h>
#include <wlr/render/vulkan.h>
#include <wlr/util/log.h>
#include "wayland-drm-protocol.h"

// see https://github.com/mesa3d/mesa/tree/master/src/egl/wayland/wayland-drm
// as golden reference
struct wlr_drm_buffer {
	struct wlr_vk_renderer *renderer;
	struct wlr_dmabuf_attributes attribs;
};

static void destroy_buffer(struct wl_resource *resource) {
	struct wlr_drm_buffer *buffer = wl_resource_get_user_data(resource);
	wlr_dmabuf_attributes_finish(&buffer->attribs);
	free(buffer);
}

static void buffer_handle_destroy(struct wl_client *c,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

struct wl_buffer_interface buffer_impl = {
	buffer_handle_destroy,
};

static void drm_create_buffer(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, uint32_t name,
		int32_t width, int32_t height, uint32_t stride, uint32_t format) {
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME,
		"Only prime buffer supported");
}

static void drm_create_planar_buffer(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, uint32_t name,
		int32_t width, int32_t height, uint32_t format,
		int32_t offset0, int32_t stride0,
		int32_t offset1, int32_t stride1,
		int32_t offset2, int32_t stride2) {
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME,
		"Only prime buffer supported");
}

static void drm_create_prime_buffer(struct wl_client *client,
		struct wl_resource *resource, uint32_t id, int fd,
		int32_t width, int32_t height, uint32_t format,
		int32_t offset0, int32_t stride0,
		int32_t offset1, int32_t stride1,
		int32_t offset2, int32_t stride2) {
	struct wlr_vk_renderer *renderer = wl_resource_get_user_data(resource);
	struct wlr_vk_format_modifier_props *mod = NULL;
	for (unsigned i = 0; i < renderer->format_count; ++i) {
		struct wlr_vk_format_props *props = &renderer->formats[i];
		uint32_t fmt = drm_from_shm_format(props->format.wl_format);
		if (fmt != format) {
			continue;
		}

		struct wlr_vk_format_modifier_props *fmod =
			wlr_vk_format_props_find_modifier(props, DRM_FORMAT_MOD_LINEAR);
		if (fmod && (fmod->dmabuf_flags &
				VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
			mod = fmod;
			break;
		}
	}

	if (!mod) {
		wlr_log_errno(WLR_ERROR, "Client passed unsupported format");
		wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT,
			"format not supported by renderer");
        return;
    }

	struct wlr_drm_buffer *buffer = calloc(1, sizeof(*buffer));
	if (!buffer) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		wl_resource_post_no_memory(resource);
		return;
	}

	struct wl_resource *buf_resource = wl_resource_create(client,
		&wl_buffer_interface, 1, id);
	if (!buf_resource) {
		free(buffer);
		wlr_log_errno(WLR_ERROR, "Failed to create resource");
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(buf_resource, &buffer_impl, buffer,
		destroy_buffer);

	buffer->renderer = renderer;
	buffer->attribs.modifier = mod->props.drmFormatModifier;
	buffer->attribs.width = width;
	buffer->attribs.height = height;
	buffer->attribs.format = format;
	buffer->attribs.flags = 0;
	buffer->attribs.offset[0] = offset0;
	buffer->attribs.offset[1] = offset1;
	buffer->attribs.offset[2] = offset2;
	buffer->attribs.stride[0] = stride0;
	buffer->attribs.stride[1] = stride1;
	buffer->attribs.stride[2] = stride2;
	if (buffer->attribs.stride[0] == 0) {
		wlr_log_errno(WLR_ERROR, "Client passed invalid stride0");
		wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT,
			"First plane must not have stride 0");
		return;
	}

	buffer->attribs.fd[0] = fd;
	buffer->attribs.n_planes = 3;
	for (unsigned int i = 1u; i < 3; ++i) {
		if (buffer->attribs.offset[i] == 0 && buffer->attribs.stride[i] == 0) {
			buffer->attribs.n_planes = i;
			break;
		}
		buffer->attribs.fd[i] = dup(fd);
		if (buffer->attribs.fd[i] < 0) {
			wlr_log_errno(WLR_ERROR, "dup failed");
			wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME,
				"dup on the given fd failed");
			goto error;
		}
	}

	uint32_t pcount = buffer->attribs.n_planes;
	if (pcount != mod->props.drmFormatModifierPlaneCount) {
		wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT,
			"Unexpected plane count for format");
		goto error;
	}

	return;

error:
	wl_resource_destroy(buf_resource); // will free everything
}

static void drm_authenticate(struct wl_client *client,
		struct wl_resource *resource, uint32_t id) {
	// TODO: we could try to really implement this in the backends
	// wayland: using own wl_drm
	// x11: some dri extension
	// drm: directly using libdrm authenticate
	// Not sure if worth it though as wl_drm and the whole authentication
	// thing seems to be obsolete anyways
	wl_resource_post_event(resource, WL_DRM_AUTHENTICATED);
}

static const struct wl_drm_interface drm_impl = {
	drm_authenticate,
	drm_create_buffer,
	drm_create_planar_buffer,
	drm_create_prime_buffer
};

static void bind_drm(struct wl_client *client, void *data, uint32_t version,
		uint32_t id) {
	// NOTE: clients that request version 1 can't use the prime method and
	// will therefore not be supported. We could signal that here already
	// somehow
	struct wlr_vk_renderer *renderer = data;
	struct wl_resource *resource = wl_resource_create(client, &wl_drm_interface,
		version < 2 ? version : 2, id);
	if (!resource) {
		wlr_log_errno(WLR_ERROR, "Resource creation failed");
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &drm_impl, data, NULL);
	struct wlr_vk_device *dev = renderer->dev;
	if(dev->pci.known) {
		// TODO: first check whether that file really exists.
		// otherwise use fallback (just guessing?)
		char buf[512];
		sprintf(buf, "/dev/dri/by-path/pci-%04x:%02x:%02x.%x-render",
			dev->pci.bus, dev->pci.device, dev->pci.domain, dev->pci.function);
		wl_drm_send_device(resource, buf);
		wlr_log(WLR_INFO, "sending device path %s", buf);
	} else {
		// TODO: not sure what to do in this case really...
		wl_drm_send_device(resource, "/dev/dri/renderD128");
	}
	for (unsigned i = 0; i < renderer->format_count; ++i) {
		struct wlr_vk_format_props *props = &renderer->formats[i];
		struct wlr_vk_format_modifier_props *mod =
			wlr_vk_format_props_find_modifier(props, DRM_FORMAT_MOD_LINEAR);
		if (mod && (mod->dmabuf_flags &
				VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
			uint32_t fmt = drm_from_shm_format(props->format.wl_format);
			wl_drm_send_format(resource, fmt);
		}
	}
	if (version >= 2) {
		wl_drm_send_capabilities(resource, WL_DRM_CAPABILITY_PRIME);
	}
}

void vulkan_bind_wl_drm(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	renderer->wl_drm = wl_global_create(wl_display,
		&wl_drm_interface, 2, renderer, bind_drm);
}

bool vulkan_resource_is_wl_drm_buffer(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource) {
	if (!wl_resource_instance_of(resource, &wl_buffer_interface,
			&buffer_impl)) {
		return false;
	}

	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct wlr_drm_buffer *buffer = wl_resource_get_user_data(resource);
	return (buffer && buffer->renderer == renderer);
}

struct wlr_texture *vulkan_texture_from_wl_drm(
		struct wlr_renderer *wlr_renderer, struct wl_resource *resource) {
	if (!wl_resource_instance_of(resource, &wl_buffer_interface,
			&buffer_impl)) {
		wlr_log(WLR_ERROR, "Invalid resource");
		return NULL;
	}

	struct wlr_drm_buffer *buffer = wl_resource_get_user_data(resource);
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (!buffer || !(buffer->renderer == renderer)) {
		wlr_log(WLR_ERROR, "Invalid resource (renderer)");
		return NULL;
	}

	return wlr_vk_texture_from_dmabuf(renderer, &buffer->attribs);
}

void vulkan_wl_drm_buffer_get_size(struct wlr_renderer *wlr_renderer,
		struct wl_resource *resource, int *width, int *height) {
	if (!wl_resource_instance_of(resource, &wl_buffer_interface,
			&buffer_impl)) {
		wlr_log(WLR_ERROR, "Invalid resource");
		*width = *height = -1;
	}

	struct wlr_drm_buffer *buffer = wl_resource_get_user_data(resource);
	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	if (!buffer || !(buffer->renderer == renderer)) {
		wlr_log(WLR_ERROR, "Invalid resource (renderer)");
		*width = *height = -1;
	}

	*width = buffer->attribs.width;
	*height = buffer->attribs.height;
}

