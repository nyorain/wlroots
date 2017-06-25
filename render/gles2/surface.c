#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-util.h>
#include <wayland-server-protocol.h>
#include <wlr/render.h>
#include <wlr/render/interface.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"
#include "backend/egl.h"

static void gles2_surface_gen_texture(struct wlr_surface_state *surface) {
	if (surface->tex_id)
		return;

	GL_CALL(glGenTextures(1, &surface->tex_id));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
}

static bool gles2_surface_attach_pixels(struct wlr_surface_state *surface,
		enum wl_shm_format format, int width, int height,
		const unsigned char *pixels) {
	assert(surface);
	const struct pixel_format *fmt = gl_format_for_wl_format(format);
	if (!fmt || !fmt->gl_format) {
		wlr_log(L_ERROR, "No supported pixel format for this surface");
		return false;
	}
	surface->wlr_surface->width = width;
	surface->wlr_surface->height = height;
	surface->wlr_surface->format = format;
	surface->pixel_format = fmt;

	gles2_surface_gen_texture(surface);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
			fmt->gl_format, fmt->gl_type, pixels));
	surface->wlr_surface->valid = true;
	return true;
}

static bool gles2_surface_attach_shm(struct wlr_surface_state *surface,
		uint32_t format, struct wl_shm_buffer *shm) {
	uint32_t width = wl_shm_buffer_get_width(shm);
	uint32_t height = wl_shm_buffer_get_height(shm);
	uint32_t stride = wl_shm_buffer_get_stride(shm);

	surface->wlr_surface->width = width;
	surface->wlr_surface->height = height;
	surface->wlr_surface->format = format;

	const struct pixel_format *pf = gl_format_for_wl_format(format);
	if (pf == NULL) {
		wlr_log(L_ERROR, "Unsupported shm format");
		return false;
  	}

	gles2_surface_gen_texture(surface);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));

	// TODO: try to achieve this without gl extensions?
	GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (pf->bpp / 8)));

	wl_shm_buffer_begin_access(shm);
	void *pixels = wl_shm_buffer_get_data(shm);
	GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, pf->gl_format, width, height, 0,
			pf->gl_format, GL_UNSIGNED_BYTE, pixels));
	wl_shm_buffer_end_access(shm);

	surface->wlr_surface->valid = true;
	return true;
}

static bool gles2_surface_attach_drm(struct wlr_surface_state *surface,
		uint32_t format, struct wl_resource* buf) {
	if (!surface->renderer->glEGLImageTargetTexture2DOES)
		return false;

	struct wlr_egl *egl = surface->renderer->egl;
	if (!egl->wl_display)
		return false;

	wlr_egl_query_buffer(egl, buf, EGL_WIDTH, (EGLint*) &surface->wlr_surface->width);
	wlr_egl_query_buffer(egl, buf, EGL_HEIGHT, (EGLint*) &surface->wlr_surface->height);

	EGLint inverted_y;
	wlr_egl_query_buffer(egl, buf, EGL_WAYLAND_Y_INVERTED_WL, &inverted_y);

	GLenum target;
	switch (format) {
	case EGL_TEXTURE_RGB:
	case EGL_TEXTURE_RGBA:
		target = GL_TEXTURE_2D;
		break;
	case EGL_TEXTURE_EXTERNAL_WL:
		target = GL_TEXTURE_EXTERNAL_OES;
		break;
	default:
		wlr_log(L_ERROR, "invalid/unsupported egl buffer format");
		return false;
   }

	gles2_surface_gen_texture(surface);
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));

	EGLint attribs[] = { EGL_WAYLAND_PLANE_WL, 0, EGL_NONE };
	surface->image = wlr_egl_create_image(egl, EGL_WAYLAND_BUFFER_WL, buf, attribs);
	if (!surface->image) {
		wlr_log(L_ERROR, "failed to create egl image: %s", egl_error());
 		return false;
	}

	GL_CALL(glActiveTexture(GL_TEXTURE0));
	GL_CALL(glBindTexture(target, surface->tex_id));
	GL_CALL(surface->renderer->glEGLImageTargetTexture2DOES(target, surface->image));
	return true;
}

static void gles2_surface_get_matrix(struct wlr_surface_state *surface,
		float (*matrix)[16], const float (*projection)[16], int x, int y) {
	struct wlr_surface *_surface = surface->wlr_surface;
	float world[16];
	wlr_matrix_identity(matrix);
	wlr_matrix_translate(&world, x, y, 0);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_scale(&world, _surface->width, _surface->height, 1);
	wlr_matrix_mul(matrix, &world, matrix);
	wlr_matrix_mul(projection, matrix, matrix);
}

static void gles2_surface_bind(struct wlr_surface_state *surface) {
	GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
	GL_CALL(glBindTexture(GL_TEXTURE_2D, surface->tex_id));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	GL_CALL(glUseProgram(*surface->pixel_format->shader));
}

static void gles2_surface_destroy(struct wlr_surface_state *surface) {
	if (surface->tex_id)
		GL_CALL(glDeleteTextures(1, &surface->tex_id));

	if (surface->image)
		wlr_egl_destroy_image(surface->renderer->egl, surface->image);

	free(surface);
}

static struct wlr_surface_impl wlr_surface_impl = {
	.attach_pixels = gles2_surface_attach_pixels,
	.attach_shm = gles2_surface_attach_shm,
	.attach_drm = gles2_surface_attach_drm,
	.get_matrix = gles2_surface_get_matrix,
	.bind = gles2_surface_bind,
	.destroy = gles2_surface_destroy,
};

struct wlr_surface *gles2_surface_init(struct wlr_renderer_state *renderer) {
	struct wlr_surface_state *state = calloc(sizeof(struct wlr_surface_state), 1);
	struct wlr_surface *surface = wlr_surface_init(state, &wlr_surface_impl);
	state->wlr_surface = surface;
	state->renderer = renderer;
	return surface;
}
