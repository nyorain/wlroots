/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_WLR_RENDERER_H
#define WLR_RENDER_WLR_RENDERER_H

#include <stdint.h>
#include <wayland-server-protocol.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

struct wlr_renderer_impl;

struct wlr_renderer {
	const struct wlr_renderer_impl *impl;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_renderer *wlr_renderer_autocreate(struct wlr_backend *backend);

/**
 * Starts rendering on the given render_surface.
 * Call wlr_renderer_end when finished rendering
 * Only between such a begin and end can the renderer be used
 * for rendering.
 */
bool wlr_renderer_begin(struct wlr_renderer *r,
	struct wlr_render_surface *render_surface);
bool wlr_renderer_begin_output(struct wlr_renderer *r,
	struct wlr_output *output);
void wlr_renderer_end(struct wlr_renderer *r);
void wlr_renderer_clear(struct wlr_renderer *r, const float color[static 4]);
/**
 * Defines a scissor box. Only pixels that lie within the scissor box can be
 * modified by drawing functions. Providing a NULL `box` disables the scissor
 * box. The scissor may exceed the bounds of the currently rendered surface.
 */
void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box);
/**
 * Renders the requested texture.
 */
bool wlr_render_texture(struct wlr_renderer *r, struct wlr_texture *texture,
	const float projection[static 9], int x, int y, float alpha);
/**
 * Renders the requested texture using the provided matrix.
 */
bool wlr_render_texture_with_matrix(struct wlr_renderer *r,
	struct wlr_texture *texture, const float matrix[static 9], float alpha);
/**
 * Renders a solid rectangle in the specified color.
 */
void wlr_render_rect(struct wlr_renderer *r, const struct wlr_box *box,
	const float color[static 4], const float projection[static 9]);
/**
 * Renders a solid quadrangle in the specified color with the specified matrix.
 */
void wlr_render_quad_with_matrix(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Renders a solid ellipse in the specified color.
 */
void wlr_render_ellipse(struct wlr_renderer *r, const struct wlr_box *box,
	const float color[static 4], const float projection[static 9]);
/**
 * Renders a solid ellipse in the specified color with the specified matrix.
 */
void wlr_render_ellipse_with_matrix(struct wlr_renderer *r,
	const float color[static 4], const float matrix[static 9]);
/**
 * Returns a list of pixel formats supported by this renderer.
 */
const enum wl_shm_format *wlr_renderer_get_formats(struct wlr_renderer *r,
	size_t *len);
/**
 * Returns true if this wl_buffer is a wl_drm buffer.
 */
bool wlr_renderer_resource_is_wl_drm_buffer(struct wlr_renderer *renderer,
	struct wl_resource *buffer);
/**
 * Gets the width and height of a wl_drm buffer.
 */
void wlr_renderer_wl_drm_buffer_get_size(struct wlr_renderer *renderer,
	struct wl_resource *buffer, int *width, int *height);
/**
 * Get the available dmabuf formats (that can be imported).
 * Returns the number of formats returned in *formats or a negative
 * value on error. The caller must free *formats.
 */
int wlr_renderer_get_dmabuf_formats(struct wlr_renderer *renderer,
	int **formats);
/**
 * Get the available dmabuf modifiers for a given format (that can be imported).
 * Returns the number of modifiers returned in *modifiers or a negative
 * value on error. The caller must free *modifiers
 */
int wlr_renderer_get_dmabuf_modifiers(struct wlr_renderer *renderer, int format,
	uint64_t **modifiers);
/**
 * Checks if a format is supported.
 */
bool wlr_renderer_format_supported(struct wlr_renderer *r,
	enum wl_shm_format fmt);
void wlr_renderer_init_wl_display(struct wlr_renderer *r,
	struct wl_display *wl_display);
/**
 * Destroys this wlr_renderer. Textures must be destroyed separately.
 */
void wlr_renderer_destroy(struct wlr_renderer *renderer);

#endif
