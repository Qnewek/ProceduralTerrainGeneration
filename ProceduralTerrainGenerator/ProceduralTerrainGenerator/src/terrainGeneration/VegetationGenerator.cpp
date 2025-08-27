#include "VegetationGenerator.h"



//bool TerrainGenerator::GenerateVegetation()
//{
//	if (!biomeMap) {
//		std::cout << "[ERROR] BiomeMap or BiomeMapPerChunk not initialized\n";
//		return false;
//	}
//
//	PoissonGenerator::DefaultPRNG PRNG;
//	vegetationPoints.resize(width * height);
//
//	treeCount = 0;
//
//	for (int y = 0; y < height; y++) {
//		for (int x = 0; x < width; x++) {
//			const auto Points = PoissonGenerator::generatePoissonPoints(1.0f, PRNG);
//
//			for (int i = 0; i < Points.size(); i++)
//			{
//				if (GetHeightAt(Points[i].x + x, Points[i].y + y) < seeLevel)
//					continue;
//				vegetationPoints[y * width + x].push_back(std::make_pair(Points[i].x + x, Points[i].y + y));
//				treeCount++;
//			}
//		}
//	}
//
//	return true;
//}