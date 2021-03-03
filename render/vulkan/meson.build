dep_vulkan = dependency('vulkan', required: 'vulkan' in renderers)
glslang = find_program('glslangValidator', required: 'vulkan' in renderers)

# TODO: we don't really need glslang, could include the precompiled shaders
# in repo in case this makes it easier for anyone.
if not dep_vulkan.found() or not glslang.found()
	subdir_done()
endif

wlr_files += files(
	'renderer.c',
	'texture.c',
	'vulkan.c',
	'util.c',
	'pixel_format.c',
)

wlr_deps += dep_vulkan
features += { 'vulkan-renderer': true }

subdir('shaders')
