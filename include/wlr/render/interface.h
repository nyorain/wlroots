#ifndef _WLR_RENDER_INTERFACE_H
#define _WLR_RENDER_INTERFACE_H
#include <wayland-server-protocol.h>
#include <stdbool.h>
#include <wlr/render.h>
#include <wlr/types/wlr_output.h>

struct wlr_renderer_impl;
struct wlr_renderer_state;

struct wlr_renderer {
	struct wlr_renderer_impl *impl;
	struct wlr_renderer_state *state;
};

struct wlr_renderer_impl {
	void (*begin)(struct wlr_renderer_state *state, struct wlr_output *output);
	void (*end)(struct wlr_renderer_state *state);
	struct wlr_surface *(*surface_init)(struct wlr_renderer_state *state);
	bool (*render_with_matrix)(struct wlr_renderer_state *state,
		struct wlr_surface *surface, const float (*matrix)[16]);
	void (*render_quad)(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]);
	void (*render_ellipse)(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]);
	const enum wl_shm_format *(*formats)(
		struct wlr_renderer_state *state, size_t *len);
	void (*destroy)(struct wlr_renderer_state *state);
};

struct wlr_renderer *wlr_renderer_init(struct wlr_renderer_state *state,
		struct wlr_renderer_impl *impl);

struct wlr_surface_impl {
	bool (*attach_pixels)(struct wlr_surface_state *state,
		enum wl_shm_format format, int width, int height,
		const unsigned char *pixels);
	bool (*attach_shm)(struct wlr_surface_state *state, uint32_t shm_format,
		struct wl_shm_buffer *shm);
	bool (*attach_drm)(struct wlr_surface_state *state, uint32_t egl_format,
		struct wl_resource *buf);
	void (*get_matrix)(struct wlr_surface_state *state,
		float (*matrix)[16], const float (*projection)[16], int x, int y);
	void (*bind)(struct wlr_surface_state *state);
	void (*destroy)(struct wlr_surface_state *state);
};

struct wlr_surface *wlr_surface_init();
void wlr_surface_bind(struct wlr_surface *surface);

#endif
