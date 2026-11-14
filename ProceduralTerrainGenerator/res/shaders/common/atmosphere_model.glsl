//The Earth atmosphere for the sky look-up tables (AtmosphereShaders/, Sky.cpp), after Hillaire 2020, "A Scalable and Production Ready
//Sky and Atmosphere Rendering Technique": Rayleigh scattering (blue sky, red sunsets), Mie scattering (white haze, glow around the sun)
//and ozone absorption (the blue of the sky at dusk). Distances in kilometres, from the centre of the planet

const float PI = 3.14159265;
const float GROUND_RADIUS = 6360.0;
const float TOP_RADIUS = 6460.0;
const vec3 RAYLEIGH_SCATTERING = vec3(5.802, 13.558, 33.1) * 1e-3;     //per km at the ground
const float RAYLEIGH_HEIGHT = 8.0;                                      //km, density falls by e
const float MIE_SCATTERING = 3.996e-3;
const float MIE_EXTINCTION = 4.440e-3;
const float MIE_HEIGHT = 1.2;
const float MIE_G = 0.8;                                                //forward scattering of the haze
const vec3 OZONE_ABSORPTION = vec3(0.650, 1.881, 0.085) * 1e-3;

struct Medium {
    vec3 rayleigh;      //scattering coefficients
    float mie;
    vec3 extinction;
};

//@param height - km above the ground
Medium mediumAt(float height)
{
    float rayleighDensity = exp(-height / RAYLEIGH_HEIGHT);
    float mieDensity = exp(-height / MIE_HEIGHT);
    float ozoneDensity = max(0.0, 1.0 - abs(height - 25.0) / 15.0);
    Medium m;
    m.rayleigh = RAYLEIGH_SCATTERING * rayleighDensity;
    m.mie = MIE_SCATTERING * mieDensity;
    m.extinction = m.rayleigh + MIE_EXTINCTION * mieDensity + OZONE_ABSORPTION * ozoneDensity;
    return m;
}

float rayleighPhase(float cosAngle)
{
    return 3.0 / (16.0 * PI) * (1.0 + cosAngle * cosAngle);
}

//Cornette-Shanks
float miePhase(float cosAngle)
{
    float g2 = MIE_G * MIE_G;
    return 3.0 / (8.0 * PI) * (1.0 - g2) * (1.0 + cosAngle * cosAngle) / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * MIE_G * cosAngle, 1.5));
}

//Distance along a unit direction from origin to a sphere around the centre of the planet, the nearest one ahead; -1 = it misses
float raySphere(vec3 origin, vec3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    float s = sqrt(discriminant);
    if (-b - s > 0.0) {
        return -b - s;
    }
    return -b + s > 0.0 ? -b + s : -1.0;
}

//Transmittance look-up table: from a point at radius r looking up at angle cosine mu to the top of the atmosphere (Bruneton's mapping)
vec2 transmittanceUV(float r, float mu)
{
    float h = sqrt(TOP_RADIUS * TOP_RADIUS - GROUND_RADIUS * GROUND_RADIUS);
    float rho = sqrt(max(r * r - GROUND_RADIUS * GROUND_RADIUS, 0.0));
    float discriminant = r * r * (mu * mu - 1.0) + TOP_RADIUS * TOP_RADIUS;
    float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
    float dMin = TOP_RADIUS - r;
    float dMax = rho + h;
    return vec2((d - dMin) / (dMax - dMin), rho / h);
}

void transmittanceParameters(vec2 uv, out float r, out float mu)
{
    float h = sqrt(TOP_RADIUS * TOP_RADIUS - GROUND_RADIUS * GROUND_RADIUS);
    float rho = h * uv.y;
    r = sqrt(rho * rho + GROUND_RADIUS * GROUND_RADIUS);
    float dMin = TOP_RADIUS - r;
    float dMax = rho + h;
    float d = dMin + uv.x * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

//Sky-view look-up table (Sky::skyViewUnit): the sky seen from the camera, x = azimuth away from the sun (0..pi, the sky is symmetric
//about the plane of the sun), y = elevation, denser near the horizon; one more row holds the light colors (SKY_COLORS_ROW)
const ivec2 SKY_VIEW_SIZE = ivec2(192, 108);
const int SKY_COLORS_ROW = 108;         //texel 0 = sunlight tint, 1 = ambient tint, 2 = transmittance towards the sun
const int SKY_VIEW_ROWS = 109;

float skyViewV(float elevation)
{
    float x = sqrt(abs(elevation) / (0.5 * PI));
    return 0.5 + 0.5 * (elevation < 0.0 ? -x : x);
}

float skyViewElevation(float v)
{
    float x = 2.0 * v - 1.0;
    return sign(x) * x * x * 0.5 * PI;
}
