#pragma once

#include <vector>

#include "WorldConfig.h"

//Editor of the NESTED terrain method (TerrainConfig::nested), shown in the Terrain tab in place of the spline editor.
//One curve at a time: the root over continentalness, a point's curve over mountainousness, its point's curve over weirdness
//(path of open points with a breadcrumb and Back). A point is a number or a curve one level down (Make sub-curve / Make constant).
//Under the curve a map of the height over C x M at a chosen W, with the coast. Edits the config in place, like the spline editor.
class NestedSplineEditor {
public:
	//@return true if the tree or its sea level changed, so the terrain has to be generated again
	bool Show(TerrainConfig& terrain);

private:
	std::vector<int> path;	//indices of the open points from the root (nested::ValidPath)
	int selected = -1;		//point of the open curve with the buttons below the plot, -1 = none
	int dragged = -1;
	//Values of the noises the open curve does not read: M for the C curve, W for the C and M curves and the map
	float contextM = 0.0f, contextW = 0.0f;
	std::vector<float> heat;
	std::vector<float> coastX, coastY;
	bool heatStale = true;
	//The tree and sea level the map shows, so a tree changed elsewhere (a loaded preset) is drawn again
	NestedCurve shownRoot;
	float shownSeaLevel = -1.0f;

	//@return true if the curve changed
	bool CurvePlot(NestedCurve& curve, float seaLevel);
	//Table of the points of the open curve: value or "curve", Open / Make sub-curve / Make constant; @return true if the tree changed
	bool PointList(const NestedCurve& root, NestedCurve& curve);
	void HeatMap(const NestedTerrain& tree);
};
