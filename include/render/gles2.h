#ifndef _WLR_RENDER_GLES2_INTERNAL_H
#define _WLR_RENDER_GLES2_INTERNAL_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include <wlr/render.h>

struct wlr_render_state {
	struct wlr_egl *egl;
};

struct pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
	GLuint *shader;
};

struct wlr_surface_state {
	struct wlr_surface *wlr_surface;
	struct wlr_render_state *renderer;
	GLuint tex_id;
	const struct pixel_format *pixel_format;
};

struct shaders {
	bool initialized;
	GLuint rgba, rgbx;
	GLuint quad;
	GLuint ellipse;
};

struct wlr_output_render_state {
	EGLSurface surface;
};

struct wlr_surface *gles2_surface_init(wlr_render_state *renderer);
extern struct shaders shaders;

const struct pixel_format *gl_format_for_wl_format(enum wl_shm_format fmt);

extern const GLchar quad_vertex_src[];
extern const GLchar quad_fragment_src[];
extern const GLchar ellipse_fragment_src[];
extern const GLchar vertex_src[];
extern const GLchar fragment_src_rgba[];
extern const GLchar fragment_src_rgbx[];

bool _gles2_flush_errors(const char *file, int line);
#define gles2_flush_errors(...) \
	_gles2_flush_errors(__FILE__ + strlen(WLR_SRC_DIR) + 1, __LINE__)

#define GL_CALL(func) func; gles2_flush_errors()

#endif
