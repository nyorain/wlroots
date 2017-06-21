#ifndef _WLR_GLES3_RENDERER_H
#define _WLR_GLES3_RENDERER_H
#include <wlr/render.h>

// Creates a vulkan renderer and attaches a wl_drm to the display that
// will be used to render client egl/vulkan buffers.
struct wlr_renderer *wlr_vulkan_renderer_init(struct wl_display *local_display);

#endif
