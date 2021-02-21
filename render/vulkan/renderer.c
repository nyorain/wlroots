#include <assert.h>
#include <vulkan/vulkan.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/util/log.h>
#include "render/vulkan.h"

static const struct wlr_renderer_impl renderer_impl;

struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer->impl == &renderer_impl);
	return (struct wlr_vk_renderer *)wlr_renderer;
}

