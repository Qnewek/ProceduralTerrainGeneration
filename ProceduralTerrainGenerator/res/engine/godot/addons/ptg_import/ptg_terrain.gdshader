// Terrain of a Procedural Terrain Generator pack: up to 16 surface materials mixed by the splat maps of the pack
// (4 materials per RGBA file, shares summing to 1), textures repeated every tile_metres in world X and Z.
// Without materials the base color map (the biome colors of the pack) or plain grey.
shader_type spatial;

uniform bool use_materials = false;
uniform int material_count = 0;
uniform sampler2D splat_0 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_1 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_2 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2D splat_3 : hint_default_black, filter_linear, repeat_disable;
uniform sampler2DArray albedo_array : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray normal_array : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray roughness_array : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float tile_metres[16];
uniform sampler2D base_color_map : source_color, hint_default_white, filter_linear, repeat_disable;

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	if (use_materials) {
		vec4 shares[4] = vec4[4](texture(splat_0, UV), texture(splat_1, UV), texture(splat_2, UV), texture(splat_3, UV));
		vec3 albedo = vec3(0.0);
		vec3 normal_map = vec3(0.0);
		float roughness = 0.0;
		float total = 0.0;
		// Every material sampled (no branch), so the texture derivatives stay valid
		for (int m = 0; m < material_count; m++) {
			float w = shares[m / 4][m % 4];
			vec3 uv = vec3(world_position.xz / max(tile_metres[m], 0.001), float(m));
			albedo += w * texture(albedo_array, uv).rgb;
			normal_map += w * texture(normal_array, uv).rgb;
			roughness += w * texture(roughness_array, uv).r;
			total += w;
		}
		total = max(total, 1e-4);
		ALBEDO = albedo / total;
		NORMAL_MAP = normal_map / total;
		ROUGHNESS = roughness / total;
	} else {
		ALBEDO = texture(base_color_map, UV).rgb;
		ROUGHNESS = 0.9;
	}
}
