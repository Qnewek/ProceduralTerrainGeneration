// Parameters of one noise; matches GpuNoiseParams in GpuHeightGenerator.cpp
struct NoiseParams {
	float xoffset, yoffset, scale, contrast, redistribution, lacunarity, persistence, revertGain, ridgeGain, ridgeOffset, mixPower;
	float warpStrength, warpScale, slopeErosion, terraceSharpness;
	int resolution, octaves, option, ridge, island, islandType, symmetrical, terraceSteps;
};
