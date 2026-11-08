//Sky colors, shared by the sky (sky_fragment.shader) and the fog on the terrain.
//sunDirection: unit vector from the ground towards the sun

//0 at night, 1 in full day; dusk lies between
float daylight(vec3 sunDirection)
{
    return smoothstep(-0.12, 0.3, sunDirection.y);
}

//Multiplier of the sunlight color: orange and dimmer near the horizon, white high up, like the sky around the sun
vec3 sunlightTint(vec3 sunDirection)
{
    float height = clamp(sunDirection.y, 0.0, 1.0);
    vec3 tint = mix(vec3(1.0, 0.45, 0.2), vec3(1.0), smoothstep(0.0, 0.5, height));
    return tint * mix(0.35, 1.0, smoothstep(0.0, 0.25, height));
}

//Multiplier of the ambient light: the sky lights the ground less and more warmly at dusk
vec3 ambientTint(vec3 sunDirection)
{
    return mix(vec3(0.35, 0.28, 0.32), vec3(1.0), daylight(sunDirection));
}

//Sky seen along a direction, without the sun disc; below the horizon it fades into a dark ground haze
vec3 skyColor(vec3 dir, vec3 sunDirection)
{
    dir = normalize(dir);
    float day = daylight(sunDirection);
    vec3 zenith = mix(vec3(0.015, 0.02, 0.06), vec3(0.20, 0.42, 0.80), day);
    vec3 horizon = mix(vec3(0.10, 0.09, 0.13), vec3(0.72, 0.82, 0.93), day);

    float up = max(dir.y, 0.0);
    vec3 color = mix(horizon, zenith, sqrt(up));

    //Warm glow around the sun, strongest at dawn and dusk and near the horizon
    float towardSun = max(dot(dir, sunDirection), 0.0);
    float dusk = 1.0 - smoothstep(0.0, 0.45, abs(sunDirection.y));
    color += vec3(1.0, 0.45, 0.15) * pow(towardSun, 6.0) * dusk * (1.0 - up) * 0.8;
    color += vec3(1.0, 0.9, 0.7) * pow(towardSun, 32.0) * 0.25 * smoothstep(-0.1, 0.05, sunDirection.y);

    if (dir.y < 0.0) {
        vec3 ground = horizon * 0.35;
        color = mix(color, ground, smoothstep(0.0, 0.25, -dir.y));
    }
    return color;
}

//Sun disc and its close halo, added on top of skyColor
vec3 sunDisc(vec3 dir, vec3 sunDirection)
{
    float c = dot(normalize(dir), sunDirection);
    float disc = smoothstep(0.99955, 0.99975, c);
    vec3 tint = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.9), smoothstep(0.0, 0.3, sunDirection.y));
    return tint * (disc * 2.0 + pow(max(c, 0.0), 800.0) * 0.4);
}

//Color the terrain fades into: the sky at the horizon in the direction of the view
vec3 fogColor(vec3 viewDir, vec3 sunDirection)
{
    //Never below the horizon ("flat" is a GLSL keyword)
    vec3 level = vec3(viewDir.x, max(viewDir.y, 0.0), viewDir.z);
    return skyColor(dot(level, level) > 1e-8 ? level : vec3(1.0, 0.0, 0.0), sunDirection);
}
