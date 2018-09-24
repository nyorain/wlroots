#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <stdlib.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/eventfd.h> // TODO: linux only
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/backend/vk_display.h>
#include <wlr/backend/interface.h>
#include <wlr/render/vulkan.h>
#include "backend/vk_display.h"
#include "util/signal.h"

// TODO: hotplugging support, hardware cursors, transform
// fix vt/session (back-)switching

static void *frame_wait_thread(void *data) {
	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)data;
	struct wlr_vk_display_backend *backend = output->backend;
	struct wlr_vk_device *dev = backend->renderer->dev;

	// 0.1 sec timeout; mainly relevant for destruction
	static const uint64_t timeout = 10000000;
	VkResult res;

	assert(output->frame_next && !output->frame);
	output->frame = output->frame_next;
	output->frame_next = NULL;

	// loop invariant: output->frame is valid
	for (;;) {
		res = vkWaitForFences(dev->dev, 1, &output->frame, true, timeout);
		if (res != VK_SUCCESS && res != VK_TIMEOUT) {
			// XXX: since we are a different thread this will mess
			// with output formatting
			wlr_vk_error("vkWaitForFences", res);
		}

		bool exit = false;
		assert(!pthread_mutex_lock(&output->frame_mutex)); {
			if (output->destroy) {
				exit = true;
			} else if (res == VK_SUCCESS) { // vblank fence was signaled
				vkDestroyFence(dev->dev, output->frame, NULL);
				int64_t v = 1;
				write(output->frame_fd, &v, 8);
				output->frame = VK_NULL_HANDLE;

				while (!output->frame_next) {
					pthread_cond_wait(&output->frame_cond,
						&output->frame_mutex);
				}

				output->frame = output->frame_next;
				output->frame_next = VK_NULL_HANDLE;
			}
		} assert(!pthread_mutex_unlock(&output->frame_mutex));

		if (exit) {
			return NULL;
		}
	}
}

static bool register_frame(struct wlr_vk_display_output *output) {
	struct wlr_vk_device *dev = output->backend->renderer->dev;
	VkResult res;
	int r;

	assert(!pthread_mutex_lock(&output->frame_mutex)); {
		if (output->frame_next) {
			vkDestroyFence(dev->dev, output->frame_next, NULL);
		}

		VkDisplayEventInfoEXT event_info = {0};
		event_info.sType = VK_STRUCTURE_TYPE_DISPLAY_EVENT_INFO_EXT;
		event_info.displayEvent = VK_DISPLAY_EVENT_TYPE_FIRST_PIXEL_OUT_EXT;
		res = output->backend->registerDisplayEventEXT(dev->dev,
			output->props.display, &event_info, NULL, &output->frame_next);
	} assert(!pthread_mutex_unlock(&output->frame_mutex));

	if (res != VK_SUCCESS) {
		wlr_vk_error("vkRegisterDisplayEventEXT", res);
		return false;
	}

	assert(!pthread_cond_signal(&output->frame_cond));

	if (!output->frame_thread) {
		assert(!pthread_mutex_init(&output->frame_mutex, NULL));
		assert(!pthread_cond_init(&output->frame_cond, NULL));
		r = pthread_create(&output->frame_thread, NULL,
			frame_wait_thread, output);
		if (r) {
			wlr_log(WLR_ERROR, "pthread_create: %s (%d)", strerror(r), r);
			pthread_mutex_destroy(&output->frame_mutex);
			pthread_cond_destroy(&output->frame_cond);
			return false;
		}
	}

	return true;
}

static int handle_frame(int fd, unsigned mask, void *data) {
	struct wlr_vk_display_output *output = data;
	assert(fd == output->frame_fd);
	if (!output->backend->session->active) {
		wlr_log(WLR_ERROR, "skipping frame");
	}

	wlr_log(WLR_ERROR, "frame");
	wlr_output_send_frame(&output->output);

	// reset eventfd
	int64_t v = 0;
	read(output->frame_fd, &v, 8);

	if (!register_frame(output)) {
		wlr_log(WLR_ERROR, "failed to regsiter frame");
	}

	return 1;
}

static void finish_plane(struct wlr_vk_display_backend *backend,
		struct wlr_vk_display_plane *plane) {
	if (plane->render_surface) {
		// automatically destroys the surface
		wlr_render_surface_destroy(&plane->render_surface->vk_rs.rs);
		plane->render_surface = NULL;
		plane->surface = NULL;
	}

	plane->current = NULL;
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_vk_display_backend *backend =
		wl_container_of(listener, backend, display_destroy);
	wlr_backend_destroy(&backend->backend);
}

struct wlr_vk_display_backend *get_vk_display_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_vk_display(wlr_backend));
	return (struct wlr_vk_display_backend *)wlr_backend;
}

struct wlr_vk_display_output *get_vk_display_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_vk_display(wlr_output));
	return (struct wlr_vk_display_output *)wlr_output;
}

static bool display_swap_buffers(struct wlr_output *o,
		pixman_region32_t *damage) {
	struct wlr_vk_display_output *output = get_vk_display_output(o);
	if (!output->primary) {
		wlr_log(WLR_ERROR, "no primary plane assigned");
		return false;
	}

	if (!output->backend->session->active) {
		return false;
	}

	// NOTE: we could pass VkDisplayPresentInfoKHR here if needed; requires
	// new/custom presenting entry point that allows extensions
	struct wlr_render_surface *rs = &output->primary->render_surface->vk_rs.rs;
	return wlr_render_surface_swap_buffers(rs, damage);
}

static bool display_enable(struct wlr_output *o, bool enable) {
	// TODO: can be done via the vulkan power control functionality
	// struct wlr_vk_display_output *output = get_vk_display_output(o);
	wlr_log(WLR_ERROR, "not implemented");
	return false;
}

static bool display_set_mode(struct wlr_output *o, struct wlr_output_mode *m) {
	struct wlr_vk_display_output *output = get_vk_display_output(o);
	struct wlr_vk_display_mode *mode = (struct wlr_vk_display_mode *)m;
	struct wlr_vk_display_backend *backend = output->backend;
	struct wlr_vk_device *dev = backend->renderer->dev;
	VkResult res;

	if (output->primary) {
		finish_plane(backend, output->primary);
		output->primary = NULL;
	}

    // Find a plane compatible with the display
	struct wlr_vk_display_plane *plane = NULL;
	unsigned plane_index;
    for (plane_index = 0; plane_index < backend->plane_count; ++plane_index) {
		plane = &backend->planes[plane_index];

		// TODO: realloc properly if needed
		if (plane->current && plane->current != output->props.display) {
			continue;
		}

		bool found = false;
		for (unsigned i = 0; i < plane->supported_count; ++i) {
			if (plane->supported[i] == output->props.display) {
				found = true;
				break;
			}
		}

		if (!found) {
			continue;
		}

		VkDisplayPlaneCapabilitiesKHR caps;
		res = vkGetDisplayPlaneCapabilitiesKHR(dev->phdev, mode->vk_mode,
			plane_index, &caps);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkGetDisplayPlaneCapabilitiesKHR", res);
			return false; // continue?
		}

		// TODO: check capabilities. Some way to find a cursor plane?
		// check if it matches requirements
	}

	if (!plane) {
		wlr_log(WLR_ERROR, "Could not find primary plane for output mode");
		return false;
	}

	finish_plane(backend, plane);

	VkDisplaySurfaceCreateInfoKHR surf_info = {0};
	surf_info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	surf_info.displayMode = mode->vk_mode;
	surf_info.planeIndex = plane_index;
	surf_info.planeStackIndex = plane->stack_index;
	surf_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // TODO: impl transform
	surf_info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; // TODO: check support
	surf_info.imageExtent.width = m->width;
	surf_info.imageExtent.height = m->height;

	res = vkCreateDisplayPlaneSurfaceKHR(dev->instance->instance,
		&surf_info, NULL, &plane->surface);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateDisplayPlaneSurfaceKHR", res);
		return false;
	}

	wlr_log(WLR_DEBUG, "created surface %p", (void *)plane->surface);

	plane->render_surface = vulkan_swapchain_render_surface_create(
		output->backend->renderer, m->width,
		m->height, plane->surface);
	if (!plane->render_surface) {
		wlr_log(WLR_ERROR, "Failed to create vulkan swapchain render surface");
		return false;
	}

	wlr_output_update_mode(&output->output, &mode->wlr_mode);
	wlr_output_update_enabled(&output->output, true);

	plane->current = output->props.display;
	output->primary = plane;

	// start rendering
	// bool render = !(output->frame);
	// if (render) {
	wlr_renderer_begin(&backend->renderer->wlr_renderer,
		&plane->render_surface->vk_rs.rs);
	float colors[4] = {0.f, 0.f, 0.f, 1.f};
	wlr_renderer_clear(&backend->renderer->wlr_renderer, colors);
	wlr_renderer_end(&backend->renderer->wlr_renderer);
	wlr_render_surface_swap_buffers(&plane->render_surface->vk_rs.rs, NULL);
	// }

	if (!register_frame(output)) {
		wlr_log(WLR_ERROR, "failed to regsiter frame");
		return false;
	}

	wlr_output_damage_whole(&output->output);
	return true;
}

// static bool display_set_cursor(struct wlr_output *o,
// 		struct wlr_texture *texture, int32_t scale,
// 		enum wl_output_transform transform, int32_t hotspot_x, int32_t hotspot_y,
// 		bool update_texture) {
// 	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)o;
// }

// static bool display_move_cursor(struct wlr_output *o, int x, int y) {
// 	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)o;
// }

static void display_destroy(struct wlr_output *o) {
	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)o;
	struct wlr_vk_device *dev = output->backend->renderer->dev;
	if (output->frame_thread) {
		assert(!pthread_mutex_lock(&output->frame_mutex)); {
			output->destroy = true;
		} assert(!pthread_mutex_unlock(&output->frame_mutex));
		assert(!pthread_cond_signal(&output->frame_cond));
		assert(!pthread_join(output->frame_thread, NULL));

		assert(!pthread_mutex_destroy(&output->frame_mutex));
		assert(!pthread_cond_destroy(&output->frame_cond));
	}
	if (output->frame) {
		vkDestroyFence(dev->dev, output->frame, NULL);
	}
	if (output->frame_next) {
		vkDestroyFence(dev->dev, output->frame_next, NULL);
	}
	if (output->primary) {
		finish_plane(output->backend, output->primary);
		output->primary = NULL;
	}
	if (output->frame_fd_source) {
		wl_event_source_remove(output->frame_fd_source);
	}
	if (output->frame_fd) {
		close(output->frame_fd);
	}
	wl_list_remove(&output->link);
	free(output);
}

static struct wlr_render_surface *display_get_render_surface(
		struct wlr_output *o) {
	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)o;
	if (!output->primary) {
		wlr_log(WLR_ERROR, "get_render_surface without primary plane");
		return NULL;
	}

	return &output->primary->render_surface->vk_rs.rs;
}

static void display_transform(struct wlr_output *o,
		enum wl_output_transform transform) {
	wlr_log(WLR_ERROR, "not implemented");
}

static const struct wlr_output_impl output_impl = {
	.enable = display_enable,
	.set_mode = display_set_mode,
	.transform = display_transform,
	// .set_cursor = display_set_cursor,
	// .move_cursor = display_move_cursor,
	.destroy = display_destroy,
	.swap_buffers = display_swap_buffers,
	// .set_gamma = display_set_gamma,
	// .get_gamma_size = display_get_gamma_size,
	// .export_dmabuf = display_export_dmabuf,
	.get_render_surface = display_get_render_surface,
};

static bool init_planes(struct wlr_vk_display_backend *backend) {
	struct wlr_vk_device *dev = backend->renderer->dev;
	VkResult res;

	// scan planes
	uint32_t plane_count;
	res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(dev->phdev,
		&plane_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (1)", res);
		goto error;
	}

    if (plane_count == 0) {
		wlr_log(WLR_ERROR, "Could not find any planes");
		goto error;
    }

	{
		VkDisplayPlanePropertiesKHR	plane_props[plane_count];
		res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(dev->phdev,
			&plane_count, plane_props);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (2)", res);
			goto error;
		}

		backend->plane_count = plane_count;
		backend->planes = calloc(plane_count, sizeof(*backend->planes));
		if (!backend->planes) {
			wlr_log_errno(WLR_ERROR, "allocation failed");
			goto error;
		}

		for (unsigned i = 0u; i < plane_count; ++i) {
			backend->planes[i].current = plane_props[i].currentDisplay;
			backend->planes[i].stack_index = plane_props[i].currentStackIndex;
		}
	}

	return true;

error:
	free(backend->planes);
	return false;
}

static struct wlr_vk_display_output *create_output(
		struct wlr_vk_display_backend *backend,
		const VkDisplayPropertiesKHR *props) {
	struct wlr_vk_device *dev = backend->renderer->dev;
	uint32_t modes_count;
	VkDisplayKHR display = props->display;
	VkResult res;

	// create output
	struct wlr_vk_display_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_output_init(&output->output, &backend->backend, &output_impl,
		backend->wl_display);
	output->backend = backend;
	output->props = *props;
	output->frame_fd = eventfd(0, EFD_CLOEXEC);
	if (output->frame_fd < 0) {
		wlr_log_errno(WLR_ERROR, "eventfd failed");
		goto error;
	}

	struct wl_event_loop *el = wl_display_get_event_loop(backend->wl_display);
	output->frame_fd_source = wl_event_loop_add_fd(el, output->frame_fd,
		WL_EVENT_READABLE, handle_frame, output);
	if (!output->frame_fd_source) {
		wlr_log(WLR_ERROR, "failed to create output event source");
		goto error;
	}

	// TODO: parse name? vkspec: "generally, this will be EDID"; we should
	//  test for it first.
	//  drm backend already implements edid extraction.
	output->output.phys_width = output->props.physicalDimensions.width;
	output->output.phys_height = output->props.physicalDimensions.height;

	// query modes
	res = vkGetDisplayModePropertiesKHR(dev->phdev, display,
		&modes_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkGetDisplayModePropertiesKHR (1)", res);
		goto error;
	}

	{
		assert(modes_count > 0); // guaranteed by standard
		struct VkDisplayModePropertiesKHR modes[modes_count];
		res = vkGetDisplayModePropertiesKHR(dev->phdev, display,
			&modes_count, modes);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkGetDisplayModePropertiesKHR (2)", res);
			goto error;
		}

		wlr_log(WLR_INFO, "Detected modes:");
		for (unsigned m = 0; m < modes_count; ++m) {
			struct wlr_vk_display_mode *mode = calloc(1, sizeof(*mode));
			if (!mode) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				goto error;
			}

			mode->vk_mode = modes[m].displayMode;
			mode->wlr_mode.refresh = modes[m].parameters.refreshRate;
			mode->wlr_mode.width = modes[m].parameters.visibleRegion.width;
			mode->wlr_mode.height = modes[m].parameters.visibleRegion.height;

			wlr_log(WLR_INFO, "  %"PRId32"x%"PRId32"@%"PRId32,
				mode->wlr_mode.width, mode->wlr_mode.height,
				mode->wlr_mode.refresh);
			wl_list_insert(&output->output.modes, &mode->wlr_mode.link);
		}
	}

	wl_list_insert(&backend->outputs, &output->link);
	wlr_signal_emit_safe(&backend->backend.events.new_output,
		&output->output);
	return output;

error:
	display_destroy(&output->output);
	return NULL;
}

static bool init_displays(struct wlr_vk_display_backend *backend,
		uint32_t display_count, VkDisplayPropertiesKHR *display_props) {
	struct wlr_vk_device *dev = backend->renderer->dev;
	VkResult res;

	size_t seen_len = wl_list_length(&backend->outputs);
	// +1 so length can never be 0, which is undefined behaviour.
	// Last element isn't used.
	bool seen[seen_len + 1];
	memset(seen, false, sizeof(seen));

	// create an output for each display
	for (unsigned i = 0; i < display_count; ++i) {
		// check if already known
		struct wlr_vk_display_output *it, *output = NULL;
		uint32_t index = 0;
		wl_list_for_each(it, &backend->outputs, link) {
			if (it->props.display == display_props[i].display) {
				output = it;
				break;
			}
			++index;
		}

		if (!output) {
			if (!create_output(backend, &display_props[i])) {
				wlr_log(WLR_ERROR, "Failed to create vulkan display output");
			}
		} else {
			seen[index] = true;
		}
	}

	// remove disappeared displays
	uint32_t index = wl_list_length(&backend->outputs);
	struct wlr_vk_display_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &backend->outputs, link) {
		index--;
		if (index >= seen_len || seen[index]) {
			continue;
		}

		wlr_log(WLR_INFO, "'%s' disappeared", output->output.name);
		display_destroy(&output->output);
	}

	// query display planes support
	VkDisplayKHR supported_displays[display_count];
	for (unsigned p = 0; p < backend->plane_count; ++p) {
		uint32_t supported_count = display_count;
		res = vkGetDisplayPlaneSupportedDisplaysKHR(dev->phdev, p,
			&supported_count, supported_displays);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkGetDisplayPlaneSupportedDisplaysKHR", res);
			return false;
		}

		struct wlr_vk_display_plane *plane = &backend->planes[p];
		if (supported_count > plane->supported_count) {
			plane->supported = calloc(supported_count,
				sizeof(*plane->supported));
			if (!plane->supported) {
				wlr_log_errno(WLR_ERROR, "Allocation failed");
				return false;
			}
		}

		plane->supported_count = supported_count;
		memcpy(plane->supported, supported_displays,
			supported_count * sizeof(*plane->supported));
	}

	return true;
}

static bool backend_start(struct wlr_backend *b) {
	struct wlr_vk_display_backend *backend = get_vk_display_backend(b);
	struct wlr_vk_device *dev = backend->renderer->dev;

	// scan displays
	VkResult res;
	uint32_t display_count;
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(dev->phdev,
		&display_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkGetPhysicalDeviceDisplayPropertiesKHR (1)", res);
		return false;
	}

	wlr_log(WLR_INFO, "VkDisplayKHR count: %d", display_count);
	if (display_count == 0) {
		wlr_log(WLR_ERROR, "Can't find any vulkan display");
		return false;
	}

	VkDisplayPropertiesKHR display_props[display_count];
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(dev->phdev,
		&display_count, display_props);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkGetPhysicalDeviceDisplayPropertiesKHR (2)", res);
		return false;
	}

	init_planes(backend);
	init_displays(backend, display_count, display_props);
	return true;
}

static void backend_destroy(struct wlr_backend *b) {
	if (!b) {
		return;
	}

	struct wlr_vk_display_backend *backend = get_vk_display_backend(b);
	struct wlr_vk_display_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &backend->outputs, link) {
		wlr_output_destroy(&output->output);
	}

	wlr_signal_emit_safe(&backend->backend.events.destroy, &backend->backend);
	wl_list_remove(&backend->display_destroy.link);
	wl_list_remove(&backend->session_signal.link);

	if (backend->renderer) {
		wlr_renderer_destroy(&backend->renderer->wlr_renderer);
	}

	free(backend->planes);
	free(backend);
}

struct wlr_renderer *backend_get_renderer(struct wlr_backend *b) {
	struct wlr_vk_display_backend *backend = get_vk_display_backend(b);
	return &backend->renderer->wlr_renderer;
}

static bool backend_vulkan_queue_check(struct wlr_backend *wlr_backend,
		struct wlr_vk_instance *instance, uintptr_t vk_physical_device,
		uint32_t qfam) {
	// display wsi does not offer a way to generally check for supported
	// queue families. We can only hope that it works with any graphics
	// queue.
	return true;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
	.vulkan_queue_family_present_support = backend_vulkan_queue_check,
};

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_vk_display_backend *backend =
		wl_container_of(listener, backend, session_signal);
	struct wlr_session *session = data;

	// TODO: nasty hack for mesa implementation
	// when resuming the outputs we have to first change the mode since
	// otherwise we get glitches. Recreating the output does not help;
	// the mode has be changed.
	// TODO: We could rescan display properties here (for hotplugging)
	if (session->active) {
		struct wlr_vk_display_output *output, *tmp_output;
		wl_list_for_each_safe(output, tmp_output, &backend->outputs, link) {
			struct wlr_output_mode *saved = output->output.current_mode;
			struct wlr_output_mode *mode;
			wl_list_for_each(mode, &output->output.modes, link) {
				if (mode != output->output.current_mode) {
					wlr_output_set_mode(&output->output, mode);
					wlr_output_set_mode(&output->output, saved);
					break;
				}
			}
		}
	}
}

struct wlr_backend *wlr_vk_display_backend_create(struct wl_display *display,
		struct wlr_session *session) {
	struct wlr_vk_display_backend *backend = calloc(1, sizeof(*backend));
	const char *ename;

	if (!backend) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);
	backend->wl_display = display;
	backend->session = session;

	backend->renderer = (struct wlr_vk_renderer *)
		wlr_vk_renderer_create(&backend->backend);
	if (!backend->renderer) {
		wlr_log(WLR_ERROR, "Failed to initialize vulkan renderer");
		goto error;
	}

	struct wlr_vk_device *dev = backend->renderer->dev;
	struct wlr_vk_instance *ini = dev->instance;
	ename = VK_KHR_DISPLAY_EXTENSION_NAME;
	if (!vulkan_has_extension(ini->extension_count, ini->extensions, ename)) {
		wlr_log(WLR_ERROR, "could not enable VK_KHR_DISPLAY");
		goto error;
	}

	// NOTE: we currently hard depend on this extensions.
	// It's possible to implement a timer callback driven frame timing
	// when the extension is not available
	ename = VK_EXT_DISPLAY_CONTROL_EXTENSION_NAME;
	if (!vulkan_has_extension(dev->extension_count, dev->extensions, ename)) {
		wlr_log(WLR_ERROR, "could not enable VK_EXT_DISPLAY_CONTROL");
		goto error;
	}

	backend->registerDisplayEventEXT = (PFN_vkRegisterDisplayEventEXT)
		vkGetDeviceProcAddr(dev->dev, "vkRegisterDisplayEventEXT");
	if (!backend->registerDisplayEventEXT) {
		wlr_log(WLR_ERROR, "could not load vkRegisterDisplayEventEXT");
		goto error;
	}

	backend->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &backend->session_signal);

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	wl_list_init(&backend->outputs);
	return &backend->backend;

error:
	backend_destroy(&backend->backend);
	return NULL;
}

bool wlr_backend_is_vk_display(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}

bool wlr_output_is_vk_display(struct wlr_output *output) {
	return output->impl == &output_impl;
}
