
static void destroy_drm(struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_buffer(struct wl_resource *resource) {
	// TODO
}

static void handle_buffer_destroy(struct wl_client* client, 
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wl_buffer_interface drm_buffer_impl = {
	.destroy = handle_buffer_destroy
};

static void authenticate(struct wl_client *client, 
		struct wl_resource *resource, uint32_t magic) {
	wl_drm_send_authenticated(resource);
}

static void
create_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
		uint32_t name, int32_t width, int32_t height, uint32_t stride, uint32_t format) {
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_NAME,
		"wlroots: GEM names are not supported, use a PRIME fd instead");
}

static void
create_planar_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
		uint32_t name, int32_t width, int32_t height, uint32_t format,
		int32_t offset0, int32_t stride0,
		int32_t offset1, int32_t stride1,
		int32_t offset2, int32_t stride2) {
	wl_resource_post_error(resource, WL_DRM_ERROR_INVALID_FORMAT,
		"wlroots: planar buffers are not supported\n");
}

static void
create_prime_buffer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
		int32_t fd, int32_t width, int32_t height, uint32_t format,
		int32_t offset0, int32_t stride0,
		int32_t offset1, int32_t stride1,
		int32_t offset2, int32_t stride2) {
	// TODO
	struct wlr_renderer_state *state = wl_resource_get_user_data(resource);
	assert(state);

	struct wl_resource *buf = wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buf, &drm_buffer_impl, state, destroy_buffer);

	// import image using the fd and vulkan import memory
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

// TODO: for bind function
// state->drm = (struct wl_drm*)
	// wl_global_create(local_display, &wl_drm_interface, 2, state, bind_drm);