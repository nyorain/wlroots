#ifndef WLR_BACKEND_VK_DISPLAY_H
#define WLR_BACKEND_VK_DISPLAY_H

#include <stdbool.h>

struct wlr_session;
struct wl_display;
struct wlr_output;
struct wlr_backend;

struct wlr_backend *wlr_vk_display_backend_create(struct wl_display *display,
	struct wlr_session *session);

bool wlr_backend_is_vk_display(struct wlr_backend *backend);
bool wlr_output_is_vk_display(struct wlr_output *output);

#endif
