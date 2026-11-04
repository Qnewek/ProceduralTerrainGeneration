// Parameters of one noise; matches GpuNoiseParams in GpuHeightGenerator.cpp
struct NoiseParams {
	float xoffset, yoffset, scale, contrast, redistribution, lacunarity, persistence, revertGain, ridgeGain, ridgeOffset, mixPower;
	int resolution, octaves, option, ridge, island, islandType, symmetrical;
};
