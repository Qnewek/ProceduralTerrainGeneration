//Octahedral impostors of the upper hemisphere; the same functions as terrainGeneration/Impostor.h (a change in one needs it in the other)

//Unit direction of the upper hemisphere (y >= 0) to [0, 1]^2: the horizon on the border of the square, straight up in its middle
vec2 hemiOctEncode(vec3 d)
{
    vec2 p = d.xz / (abs(d.x) + abs(d.y) + abs(d.z));
    return vec2(p.x + p.y, p.x - p.y) * 0.5 + 0.5;
}

//Axes of the image seen from direction d (unit, from the model to the viewer): right and up, up as close to +y as possible
void frameAxes(vec3 d, out vec3 right, out vec3 up)
{
    vec3 r = cross(vec3(0.0, 1.0, 0.0), d);
    float len = length(r);
    right = len > 1e-4 ? r / len : vec3(1.0, 0.0, 0.0);
    up = cross(d, right);
}
