#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform UBO {
	layout(offset = 80) float alpha;
} data;

void main() {
	out_color = texture(tex, uv);

	if (data.alpha < 0.0) {
		out_color.a = -data.alpha;
	} else {
		out_color.a *= data.alpha;
	}
}

