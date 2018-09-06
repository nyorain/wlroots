#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/backend/vk_display.h>
#include <wlr/backend/interface.h>
#include <wlr/render/vulkan.h>
#include "backend/vk_display.h"
#include "util/signal.h"

// TODO: hotplugging support, hardware cursors, transform
// TODO: remove in favor of real vblank timing
static int signal_frame(void *data) {
	wlr_log(WLR_ERROR, "frame!");
	struct wlr_vk_display_output *output = data;
	wlr_output_send_frame(&output->output);
	wl_event_source_timer_update(output->frame_timer,
		1000000 / output->output.current_mode->refresh);
	return 0;
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

	wlr_log(WLR_ERROR, "swap buffers!");
	// TODO: pass VkDisplayPresentInfoKHR, requires custom
	// swpchain render_surface function
	struct wlr_render_surface *rs = &output->primary->render_surface->vk_rs.rs;
	return wlr_render_surface_swap_buffers(rs, damage);
}

static bool display_enable(struct wlr_output *o, bool enable) {
	// struct wlr_vk_display_output *output = get_vk_display_output(o);
	wlr_log(WLR_ERROR, "not implemented");
	return false;
}

static bool display_set_mode(struct wlr_output *o, struct wlr_output_mode *m) {
	struct wlr_vk_display_output *output = get_vk_display_output(o);
	struct wlr_vk_display_mode *mode = (struct wlr_vk_display_mode *)m;
	struct wlr_vk_display_backend *backend = output->backend;
	struct wlr_vulkan *vulkan = backend->renderer->vulkan;
	VkResult res;

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
		res = vkGetDisplayPlaneCapabilitiesKHR(vulkan->phdev, mode->vk_mode,
			plane_index, &caps);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkGetDisplayPlaneCapabilitiesKHR", res);
			return false; // continue?
		}

		// TODO: handle them. Some way to find a cursor plane?
		// check if it matches requirements
	}

	if (!plane) {
		wlr_log(WLR_ERROR, "Could not find primary plane for output mode");
		return false;
	}

	if (plane->render_surface) {
		wlr_render_surface_destroy(&plane->render_surface->vk_rs.rs);
		plane->render_surface = NULL;
	}

	if (plane->surface) {
		vkDestroySurfaceKHR(vulkan->instance, plane->surface, NULL);
		plane->surface = NULL;
	}

	VkDisplaySurfaceCreateInfoKHR surf_info = {0};
	surf_info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	surf_info.displayMode = mode->vk_mode;
	surf_info.planeIndex = plane_index;
	surf_info.planeStackIndex = plane->stack_index;
	surf_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // TODO: impl transform
	surf_info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR; // TODO: check support
	surf_info.imageExtent.width = m->width;
	surf_info.imageExtent.height = m->height;

	res = vkCreateDisplayPlaneSurfaceKHR(vulkan->instance,
		&surf_info, NULL, &plane->surface);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkCreateDisplayPlaneSurfaceKHR", res);
		return false;
	}

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
	wlr_renderer_begin(&backend->renderer->wlr_renderer,
		&plane->render_surface->vk_rs.rs);
	float colors[4] = {0.f, 0.f, 0.f, 1.f};
	wlr_renderer_clear(&backend->renderer->wlr_renderer, colors);
	wlr_renderer_end(&backend->renderer->wlr_renderer);
	wlr_render_surface_swap_buffers(&plane->render_surface->vk_rs.rs, NULL);

	// TODO: this should be done using VK_EXT_display_control. To do this
	// correctly we have to wait on the vblank fence in a new thread then signal
	// the main thread to render.
	if (!output->frame_timer) {
		struct wl_event_loop *ev = wl_display_get_event_loop(backend->wl_display);
		output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);
	}

	wl_event_source_timer_update(output->frame_timer,
		1000000 / mode->wlr_mode.refresh);
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
	free(output);
}

static struct wlr_render_surface *display_get_render_surface(
		struct wlr_output *o) {
	struct wlr_vk_display_output *output = (struct wlr_vk_display_output *)o;
	if (!output->primary) {
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
	struct wlr_vulkan *vulkan = backend->renderer->vulkan;
	VkResult res;

	// XXX: seems like we have to call this first?
	// could be a bug in the current mesa implementation since the spec
	// doesn't mention this
	// otherwise we get no planes
	uint32_t display_count;
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(vulkan->phdev,
		&display_count, NULL);

	// scan planes
	uint32_t plane_count;
	res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vulkan->phdev,
		&plane_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (1)", res);
		goto error;
	}

    if (plane_count == 0) {
		wlr_log(WLR_ERROR, "Could not find any planes");
		goto error;
    }

	{
		VkDisplayPlanePropertiesKHR	plane_props[plane_count];
		res = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(vulkan->phdev,
			&plane_count, plane_props);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkGetPhysicalDeviceDisplayPlanePropertiesKHR (2)", res);
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

static bool create_output(struct wlr_vk_display_backend *backend,
		const VkDisplayPropertiesKHR *props) {
	struct wlr_vulkan *vulkan = backend->renderer->vulkan;
	uint32_t modes_count;
	VkDisplayKHR display = props->display;
	VkResult res;

	// create output
	struct wlr_vk_display_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		goto error;
	}

	wlr_output_init(&output->output, &backend->backend, &output_impl,
		backend->wl_display);
	output->backend = backend;
	output->props = *props;

	// TODO: parse name? vkspec: "generally, this will be EDID"; we should
	//  test for it first. Some way to give this display a normal name?
	output->output.phys_width = output->props.physicalDimensions.width;
	output->output.phys_height = output->props.physicalDimensions.height;

	// query modes
	res = vkGetDisplayModePropertiesKHR(vulkan->phdev, display,
		&modes_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkGetDisplayModePropertiesKHR (1)", res);
		goto error;
	}

	{
		assert(modes_count > 0); // guaranteed by standard
		struct VkDisplayModePropertiesKHR modes[modes_count];
		res = vkGetDisplayModePropertiesKHR(vulkan->phdev, display,
			&modes_count, modes);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkGetDisplayModePropertiesKHR (2)", res);
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
	return true;

error:
	display_destroy(&output->output);
	return false;
}

static bool scan_displays(struct wlr_vk_display_backend *backend) {
	struct wlr_vulkan *vulkan = backend->renderer->vulkan;
	VkResult res;

	// scan displays
	uint32_t display_count;
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(vulkan->phdev,
		&display_count, NULL);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkGetPhysicalDeviceDisplayPropertiesKHR (1)", res);
		return false;
	}

	if (display_count == 0) {
		wlr_log(WLR_ERROR, "Can't find any vulkan display");
		return false;
	}

	VkDisplayPropertiesKHR display_props[display_count];
	res = vkGetPhysicalDeviceDisplayPropertiesKHR(vulkan->phdev,
		&display_count, display_props);
	if (res != VK_SUCCESS) {
		wlr_vulkan_error("vkGetPhysicalDeviceDisplayPropertiesKHR (2)", res);
		return false;
	}

	// create an output for each display
	for (unsigned i = 0; i < display_count; ++i) {
		if (!create_output(backend, &display_props[i])) {
			wlr_log(WLR_ERROR, "Failed to create vulkan display output");
		}
	}

	// query display planes support
	VkDisplayKHR supported_displays[display_count];
	for (unsigned p = 0; p < backend->plane_count; ++p) {
		uint32_t supported_count = display_count;
		res = vkGetDisplayPlaneSupportedDisplaysKHR(vulkan->phdev, p,
			&supported_count, supported_displays);
		if (res != VK_SUCCESS) {
			wlr_vulkan_error("vkGetDisplayPlaneSupportedDisplaysKHR", res);
			return false;
		}

		// uint32_t supported_count;
		// res = vkGetDisplayPlaneSupportedDisplaysKHR(vulkan->phdev, p,
		// 	&supported_count, NULL);
		// VkDisplayKHR supported_displays[supported_count + 1];
		// res = vkGetDisplayPlaneSupportedDisplaysKHR(vulkan->phdev, p,
		// 	&supported_count, supported_displays);
		// if (res != VK_SUCCESS) {
		// 	wlr_vulkan_error("vkGetDisplayPlaneSupportedDisplaysKHR", res);
		// 	return false;
		// }

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
	scan_displays(backend);
	return true;
}

static void backend_destroy(struct wlr_backend *b) {
	// struct wlr_vk_display_backend *backend = get_vk_display_backend(b);
	// TODO
}

struct wlr_renderer *backend_get_renderer(struct wlr_backend *b) {
	struct wlr_vk_display_backend *backend = get_vk_display_backend(b);
	return &backend->renderer->wlr_renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

struct wlr_backend *wlr_vk_display_backend_create(struct wl_display *display) {
	struct wlr_vk_display_backend *backend = calloc(1, sizeof(*backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);
	backend->wl_display = display;

	// create vulkan and renderer
	bool debug = false;
	const char *name = VK_KHR_DISPLAY_EXTENSION_NAME;
	struct wlr_vulkan *vulkan = wlr_vulkan_create(1, &name, 0, NULL, debug);
	if (!vulkan) {
		wlr_log(WLR_ERROR, "Failed to initialize vulkan");
		goto error;
	}

	backend->renderer = (struct wlr_vk_renderer *)
		wlr_vk_renderer_create_for_vulkan(&backend->backend, vulkan);
	if (!backend->renderer) {
		wlr_log(WLR_ERROR, "Failed to initialize vulkan renderer");
		goto error;
	}

	if (!init_planes(backend)) {
		goto error;
	}

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
