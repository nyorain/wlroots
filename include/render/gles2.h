#ifndef RENDER_GLES2_H
#define RENDER_GLES2_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/wlr_render_surface.h>
#include <wlr/util/log.h>

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

struct wlr_gles2_pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
	bool has_alpha;
};

struct wlr_gles2_tex_shader {
	GLuint program;
	GLint proj;
	GLint invert_y;
	GLint tex;
	GLint alpha;
};

struct wlr_gles2_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;

	struct wlr_egl egl;
	const char *exts_str;

	struct {
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} quad;
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} ellipse;
		struct wlr_gles2_tex_shader tex_rgba;
		struct wlr_gles2_tex_shader tex_rgbx;
		struct wlr_gles2_tex_shader tex_ext;
	} shaders;

	uint32_t viewport_width, viewport_height;
};

enum wlr_gles2_texture_type {
	WLR_GLES2_TEXTURE_GLTEX,
	WLR_GLES2_TEXTURE_WL_DRM_GL,
	WLR_GLES2_TEXTURE_WL_DRM_EXT,
	WLR_GLES2_TEXTURE_DMABUF,
};

struct wlr_gles2_texture {
	struct wlr_texture wlr_texture;

	struct wlr_egl *egl;
	enum wlr_gles2_texture_type type;
	int width, height;
	bool has_alpha;
	bool inverted_y;

	// Not set if WLR_GLES2_TEXTURE_GLTEX
	EGLImageKHR image;
	GLuint image_tex;

	union {
		GLuint gl_tex;
		struct wl_resource *wl_drm;
	};
};

struct wlr_gles2_render_surface {
	struct wlr_render_surface rs;
	struct wlr_gles2_renderer *renderer;
	EGLSurface surface;
	struct wl_egl_window *egl_window; // only for wayland
	bool pbuffer; // whether this is a pbuffer rs
};

struct wlr_gles2_gbm_render_surface {
	struct wlr_gles2_render_surface gles2_rs;
	struct gbm_device *gbm_dev;
	uint32_t flags;

	struct gbm_surface *gbm_surface;
	struct gbm_bo *front_bo;
	struct gbm_bo *old_front_bo;
};

const struct wlr_gles2_pixel_format *get_gles2_format_from_wl(
	enum wl_shm_format fmt);
const enum wl_shm_format *get_gles2_formats(size_t *len);

struct wlr_gles2_texture *gles2_get_texture(
	struct wlr_texture *wlr_texture);
struct wlr_gles2_renderer *gles2_get_renderer(
	struct wlr_renderer *wlr_renderer);
struct wlr_gles2_render_surface *gles2_get_render_surface(
	struct wlr_render_surface *wlr_rs);

struct wlr_render_surface *gles2_render_surface_create_headless(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height);
struct wlr_render_surface *gles2_render_surface_create_xcb(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	void *xcb_connection, uint32_t xcb_window);
struct wlr_render_surface *gles2_render_surface_create_wl(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	struct wl_display *disp, struct wl_surface *surf);
struct wlr_render_surface *gles2_render_surface_create_gbm(
	struct wlr_renderer *renderer, uint32_t width, uint32_t height,
	struct gbm_device *gbm_dev, uint32_t flags);

void push_gles2_marker(const char *file, const char *func);
void pop_gles2_marker(void);
#define PUSH_GLES2_DEBUG push_gles2_marker(_wlr_strip_path(__FILE__), __func__)
#define POP_GLES2_DEBUG pop_gles2_marker()

#endif
