#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <vulkan/vulkan.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/interface.h>
#include "render/wayland-drm-server-protocol.h"
#include "common/log.h"

// TODO: move to header
struct wlr_renderer_state {
	struct wl_drm *drm;
};

static void destroy_drm(struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_buffer(struct wl_resource *resource) {
	// TODO
}

static void handle_buffer_destroy(struct wl_client* client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wl_buffer_interface drm_buffer_impl = {
	.destroy = handle_buffer_destroy
};

static void
authenticate(struct wl_client *client, struct wl_resource *resource, uint32_t magic)
{
	wl_drm_send_authenticated(resource);
}

static void
create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
              uint32_t name, int32_t width, int32_t height, uint32_t stride, uint32_t format)
{
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME, "GEM names are not supported, use a PRIME fd instead");
}

static void
create_planar_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                     uint32_t name, int32_t width, int32_t height, uint32_t format,
                     int32_t offset0, int32_t stride0,
                     int32_t offset1, int32_t stride1,
                     int32_t offset2, int32_t stride2)
{
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT, "planar buffers are not supported\n");
}

static void
create_prime_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                    int32_t fd, int32_t width, int32_t height, uint32_t format,
                    int32_t offset0, int32_t stride0,
                    int32_t offset1, int32_t stride1,
                    int32_t offset2, int32_t stride2)
{
	// TODO
	struct wlr_renderer_state *state = wl_resource_get_user_data(resource);
	assert(state);

	struct wl_resource *buf = wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buf, &drm_buffer_impl, state, destroy_buffer);
}

static const struct wl_drm_interface drm_impl = {
	.authenticate = authenticate,
	.create_buffer = create_buffer,
	.create_planar_buffer = create_planar_buffer,
	.create_prime_buffer = create_prime_buffer,
};

static void bind_drm(struct wl_client *client, void *data,
	uint32_t version, uint32_t id) {
	struct wl_resource *res = wl_resource_create(client, &wl_drm_interface, version, id);
	wl_resource_set_implementation(res, &drm_impl, data, &destroy_drm);
}

static void vulkan_begin(struct wlr_renderer_state *state,
		struct wlr_output *output) {
}

static void vulkan_end(struct wlr_renderer_state *state) {
}

static struct wlr_surface *vulkan_surface_init(struct wlr_renderer_state *state) {
	return NULL; // TODO
}

static bool vulkan_render_surface(struct wlr_renderer_state *state,
		struct wlr_surface *surface, const float (*matrix)[16]) {
	return true;
}

static void vulkan_render_quad(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
}

static void vulkan_render_ellipse(struct wlr_renderer_state *state,
		const float (*color)[4], const float (*matrix)[16]) {
}

static void vulkan_destroy(struct wlr_renderer_state *state) {
}

static struct wlr_renderer_impl renderer_impl = {
	.begin = vulkan_begin,
	.end = vulkan_end,
	.surface_init = vulkan_surface_init,
	.render_with_matrix = vulkan_render_surface,
	.render_quad = vulkan_render_quad,
	.render_ellipse = vulkan_render_ellipse,
	.destroy = vulkan_destroy
};

struct wlr_renderer *wlr_vulkan_renderer_init(struct wl_display *local_display) {
	struct wlr_renderer_state *state;
	if (!(state = calloc(1, sizeof(*state)))) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}

	state->drm = (struct wl_drm*)
		wl_global_create(local_display, &wl_drm_interface, 2, state, bind_drm);
	return wlr_renderer_init(state, &renderer_impl);
}
