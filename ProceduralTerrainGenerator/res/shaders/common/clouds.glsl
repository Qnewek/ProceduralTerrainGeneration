//The cloud layer (Clouds.cpp): a slab between FrameBlock::cloudLayer.x and .y metres up, filled where the weather map has clouds, with
//the shape of tiling 3D noises. Shared by the ray march (CloudShaders/cloud_compute.shader) and the cloud shadows (shadow.glsl). Needs frame.glsl.
//Positions in metres: world position / cloudWind.z

layout(binding = 16) uniform sampler2D cloudWeather;       //Clouds::weatherUnit: r = coverage, g = cloud type (flat .. tall)

//Metres covered by one tile of the weather map; LightSource wraps the wind offset at a multiple of it
const float CLOUD_WEATHER_TILE = 24000.0;

bool cloudsOn()
{
    return cloudLayer.y > cloudLayer.x;
}

vec2 cloudWeatherAt(vec2 xzMetres)
{
    return texture(cloudWeather, (xzMetres + cloudWind.xy) / CLOUD_WEATHER_TILE).rg;
}

//Cloud cover of a column of the layer, 0..1, from the weather map value and the global coverage
float cloudCoverageAt(float weather)
{
    float threshold = 1.0 - cloudLayer.z;
    return smoothstep(threshold - 0.12, threshold + 0.18, weather);
}

float cloudRemap(float value, float low, float high, float newLow, float newHigh)
{
    return newLow + (value - low) / (high - low) * (newHigh - newLow);
}

//Sunlight left by the clouds over a point: the cover where the ray towards the sun crosses the middle of the layer
float cloudShadow(vec3 worldPos)
{
    if (!cloudsOn() || cloudLayer.w <= 0.0 || light.direction.y <= 0.0) {
        return 1.0;
    }
    vec3 p = worldPos / cloudWind.z;
    float middle = 0.5 * (cloudLayer.x + cloudLayer.y);
    vec2 xz = p.xz + light.direction.xz / max(light.direction.y, 0.1) * (middle - p.y);
    float cover = cloudCoverageAt(cloudWeatherAt(xz).r);
    return 1.0 - cloudLayer.w * smoothstep(0.1, 0.7, cover) * min(cloudWind.w, 1.0);
}
