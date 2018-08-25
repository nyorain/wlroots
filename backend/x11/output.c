#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/util/log.h>
#include "backend/x11.h"
#include "util/signal.h"

static int signal_frame(void *data) {
	struct wlr_x11_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

static void parse_xcb_setup(struct wlr_output *output,
		xcb_connection_t *xcb_conn) {
	const xcb_setup_t *xcb_setup = xcb_get_setup(xcb_conn);

	snprintf(output->make, sizeof(output->make), "%.*s",
			xcb_setup_vendor_length(xcb_setup),
			xcb_setup_vendor(xcb_setup));
	snprintf(output->model, sizeof(output->model), "%"PRIu16".%"PRIu16,
			xcb_setup->protocol_major_version,
			xcb_setup->protocol_minor_version);
}

static struct wlr_x11_output *get_x11_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_x11(wlr_output));
	return (struct wlr_x11_output *)wlr_output;
}

static void output_set_refresh(struct wlr_output *wlr_output, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);

	if (refresh <= 0) {
		refresh = X11_DEFAULT_REFRESH;
	}

	wlr_output_update_custom_mode(&output->wlr_output, wlr_output->width,
		wlr_output->height, refresh);

	output->frame_delay = 1000000 / refresh;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	output_set_refresh(&output->wlr_output, refresh);

	const uint32_t values[] = { width, height };
	xcb_void_cookie_t cookie = xcb_configure_window_checked(
		x11->xcb_conn, output->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

	xcb_generic_error_t *error;
	if ((error = xcb_request_check(x11->xcb_conn, cookie))) {
		wlr_log(WLR_ERROR, "Could not set window size to %dx%d\n",
			width, height);
		free(error);
		return false;
	}

	return true;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	output->wlr_output.transform = transform;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = get_x11_output_from_output(wlr_output);
	struct wlr_x11_backend *x11 = output->x11;

	wlr_input_device_destroy(&output->pointer_dev);

	wl_list_remove(&output->link);
	wl_event_source_remove(output->frame_timer);
	xcb_destroy_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);
	free(output);
}

static bool output_swap_buffers(struct wlr_output *wlr_output,
		pixman_region32_t *damage) {
	struct wlr_x11_output *output = (struct wlr_x11_output *)wlr_output;
	return wlr_render_surface_swap_buffers(output->render_surface, damage);
}

static struct wlr_render_surface *output_get_render_surface(
		struct wlr_output *wlr_output) {
	struct wlr_x11_output *output = (struct wlr_x11_output *)wlr_output;
	return output->render_surface;
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.swap_buffers = output_swap_buffers,
	.get_render_surface = output_get_render_surface,
};

struct wlr_output *wlr_x11_output_create(struct wlr_backend *backend) {
	struct wlr_x11_backend *x11 = get_x11_backend_from_backend(backend);

	if (!x11->started) {
		++x11->requested_outputs;
		return NULL;
	}

	struct wlr_x11_output *output = calloc(1, sizeof(struct wlr_x11_output));
	if (output == NULL) {
		return NULL;
	}
	output->x11 = x11;

	struct wlr_output *wlr_output = &output->wlr_output;
	wlr_output_init(wlr_output, &x11->backend, &output_impl, x11->wl_display);

	wlr_output->width = 1024;
	wlr_output->height = 768;

	output_set_refresh(&output->wlr_output, 0);

	snprintf(wlr_output->name, sizeof(wlr_output->name), "X11-%d",
		wl_list_length(&x11->outputs) + 1);
	parse_xcb_setup(wlr_output, x11->xcb_conn);

	uint32_t width = wlr_output->width;
	uint32_t height = wlr_output->height;
	uint32_t mask = XCB_CW_EVENT_MASK;
	uint32_t values[] = {
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};
	output->win = xcb_generate_id(x11->xcb_conn);
	xcb_create_window(x11->xcb_conn, XCB_COPY_FROM_PARENT, output->win,
		x11->screen->root, 0, 0, width, height, 1, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		x11->screen->root_visual, mask, values);

	output->render_surface = wlr_renderer_create_render_surface(
		x11->renderer, &output->win, width, height);
	if (!output->render_surface) {
		wlr_log(WLR_ERROR, "Failed to create render surface");
		free(output);
		return NULL;
	}

	xcb_change_property(x11->xcb_conn, XCB_PROP_MODE_REPLACE, output->win,
		x11->atoms.wm_protocols, XCB_ATOM_ATOM, 32, 1,
		&x11->atoms.wm_delete_window);

	char title[32];
	if (snprintf(title, sizeof(title), "wlroots - %s", wlr_output->name)) {
		xcb_change_property(x11->xcb_conn, XCB_PROP_MODE_REPLACE, output->win,
			x11->atoms.net_wm_name, x11->atoms.utf8_string, 8,
			strlen(title), title);
	}

	uint32_t cursor_values[] = { x11->cursor };
	xcb_change_window_attributes(x11->xcb_conn, output->win, XCB_CW_CURSOR,
		cursor_values);

	xcb_map_window(x11->xcb_conn, output->win);
	xcb_flush(x11->xcb_conn);

	struct wl_event_loop *ev = wl_display_get_event_loop(x11->wl_display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&x11->outputs, &output->link);

	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	wlr_output_update_enabled(wlr_output, true);

	wlr_input_device_init(&output->pointer_dev, WLR_INPUT_DEVICE_POINTER,
		&input_device_impl, "X11 pointer", 0, 0);
	wlr_pointer_init(&output->pointer, &pointer_impl);
	output->pointer_dev.pointer = &output->pointer;
	output->pointer_dev.output_name = strdup(wlr_output->name);

	wlr_signal_emit_safe(&x11->backend.events.new_output, wlr_output);
	wlr_signal_emit_safe(&x11->backend.events.new_input, &output->pointer_dev);

	return wlr_output;
}

void handle_x11_configure_notify(struct wlr_x11_output *output,
		xcb_configure_notify_event_t *ev) {
	// ignore events that set an invalid size:
	if (ev->width > 0 && ev->height > 0) {
		wlr_output_update_custom_mode(&output->wlr_output, ev->width,
			ev->height, output->wlr_output.refresh);
		wlr_render_surface_resize(output->render_surface,
			ev->width, ev->height);

		// Move the pointer to its new location
		update_x11_pointer_position(output, output->x11->time);
	} else {
		wlr_log(WLR_DEBUG,
			"Ignoring X11 configure event for height=%d, width=%d",
			ev->width, ev->height);
	}
}

bool wlr_output_is_x11(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}
