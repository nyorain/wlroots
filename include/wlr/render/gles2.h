#ifndef _WLR_GLES2_RENDERER_H
#define _WLR_GLES2_RENDERER_H
#include <wlr/render.h>
#include "backend/egl.h"

struct wlr_renderer *wlr_gles2_renderer_init(struct wlr_egl *egl);

#endif
