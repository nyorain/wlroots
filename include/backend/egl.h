#ifndef WLR_BACKEND_EGL_H
#define WLR_BACKEND_EGL_H

#include <EGL/egl.h>
#include <stdbool.h>

struct wlr_egl_api;

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	wl_display* wl_display;
	wlr_egl_api* api;
};

// Initializes an egl context for the given platform and remote display.
// Will attempt to load all possibly required api functions.
bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *display)

// Frees all related egl resources, makes the context not-current and
// unbinds a bound wayland display.
void wlr_egl_free(struct wlr_egl *egl);

// Binds the given display to the egl instance.
// This will allow clients to create egl surfaces from wayland ones and render to it.
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

// Queries information about the given (potential egl/drm) buffer.
// Refer to eglQueryWaylandBufferWL for more information about attrib and return value.
// Makes only sense when a wl_display was bound to it since otherwise there
// cannot be any egl/drm buffers.
int wlr_egl_query_buffer(struct wlr_egl *egl, struct wl_buffer *buf, int attrib);

// Returns a surface for the given native window
// The window must match the remote display the wlr_egl was created with.
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

// Returns a string for the last error ocurred with egl.
const char *egl_error(void);

#endif
