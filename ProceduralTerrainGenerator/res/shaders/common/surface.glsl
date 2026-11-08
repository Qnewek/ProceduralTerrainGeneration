//Shape of the displayed surface from a texel of the surface map (surface_compute.shader); same formulas as
//surface::Normal, Slope and Curvature in SurfaceDerivatives.cpp. The map holds raw height derivatives,
//so heightScale is applied here.

//Unit normal before the model matrix; x and z follow map x and y, y points up
vec3 surfaceNormal(vec4 derivatives, float heightScale)
{
    return normalize(vec3(-derivatives.x * heightScale, 1.0, -derivatives.y * heightScale));
}

//Angle to the horizontal plane in radians
float surfaceSlope(vec4 derivatives, float heightScale)
{
    return atan(length(derivatives.xy) * abs(heightScale));
}

//Laplacian of the displayed surface in 1 / world unit; positive in hollows, negative on ridges
float surfaceCurvature(vec4 derivatives, float heightScale)
{
    return derivatives.z * heightScale;
}
