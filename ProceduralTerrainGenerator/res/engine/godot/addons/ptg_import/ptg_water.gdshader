// Water of a Procedural Terrain Generator pack (sea, rivers and lakes): translucent, glossy, small moving ripples.
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_opaque;

uniform vec4 water_color : source_color = vec4(0.05, 0.22, 0.30, 0.75);
uniform float ripple_size = 6.0;     // metres
uniform float ripple_speed = 0.6;

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 p = world_position.xz / max(ripple_size, 0.001);
	float t = TIME * ripple_speed;
	vec2 slope = vec2(cos(p.x * 1.7 + t) + 0.5 * cos(p.y * 2.3 - 1.3 * t), cos(p.y * 1.9 + 0.8 * t) + 0.5 * cos((p.x + p.y) * 1.1 - t));
	NORMAL_MAP = normalize(vec3(slope * 0.06, 1.0)) * 0.5 + 0.5;
	ALBEDO = water_color.rgb;
	ALPHA = water_color.a;
	ROUGHNESS = 0.05;
	METALLIC = 0.0;
	SPECULAR = 0.6;
}
