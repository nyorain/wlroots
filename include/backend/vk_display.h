#ifndef BACKEND_VK_DISPLAY_H
#define BACKEND_VK_DISPLAY_H

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <render/vulkan.h>
#include <vulkan/vulkan.h>
#include <pthread.h>

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
	struct wlr_session *session;
	struct wl_list outputs;
	struct wl_listener display_destroy;

	uint32_t plane_count;
	struct wlr_vk_display_plane *planes;

	struct wl_listener session_signal;

	PFN_vkRegisterDisplayEventEXT registerDisplayEventEXT;
};

struct wlr_vk_display_output {
	struct wlr_output output;

	struct wl_list link;
	struct wlr_vk_display_backend *backend;
	VkDisplayPropertiesKHR props;

	pthread_t frame_thread; // waits for vblank fence
	pthread_cond_t frame_cond; // signaled when frame_next is set
	pthread_mutex_t frame_mutex; // for 3 vars below
	VkFence frame;
	VkFence frame_next;
	bool destroy;
	int frame_fd;
	struct wl_event_source *frame_fd_source;

	uint32_t supported_planes_count;
	struct wlr_vk_display_plane **suppported_planes;

	struct wlr_vk_display_plane *primary;
	// struct wlr_vk_display_plane *cursor;

	// TODO
	// struct wl_event_source *frame_timer;
};

struct wlr_vk_display_mode {
	struct wlr_output_mode wlr_mode;
	VkDisplayModeKHR vk_mode;
};

#endif
