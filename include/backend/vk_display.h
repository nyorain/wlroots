#ifndef BACKEND_WAYLAND_H
#define BACKEND_WAYLAND_H

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <render/vulkan.h>
#include <vulkan/vulkan.h>

struct wlr_vk_display_plane {
	VkDisplayKHR current;
	uint32_t stack_index;

	uint32_t supported_count;
	VkDisplayKHR *supported;

	// only set when plane is used
	VkSurfaceKHR surface;
	struct wlr_vk_swapchain_render_surface *render_surface;
	// VkDisplayPlaneCapabilitiesKHR
};

struct wlr_vk_display_backend {
	struct wlr_backend backend;
	struct wlr_vk_renderer *renderer;
	struct wl_display *wl_display;
	struct wl_list outputs;
	struct wl_listener display_destroy;

	uint32_t plane_count;
	struct wlr_vk_display_plane *planes;
};

struct wlr_vk_display_output {
	struct wlr_output output;

	struct wl_list link;
	struct wlr_vk_display_backend *backend;
	VkDisplayPropertiesKHR props;

	uint32_t supported_planes_count;
	struct wlr_vk_display_plane **suppported_planes;

	struct wlr_vk_display_plane *primary;
	// struct wlr_vk_display_plane *cursor;

	// TODO
	struct wl_event_source *frame_timer;
};

struct wlr_vk_display_mode {
	struct wlr_output_mode wlr_mode;
	VkDisplayModeKHR vk_mode;
};

#endif
