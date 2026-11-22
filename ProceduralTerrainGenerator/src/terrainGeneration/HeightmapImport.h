#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

#include "GeoReference.h"
#include "Grid.h"
#include "HeightmapExport.h"

//Reading height maps from files. The format is chosen by the file extension; a new format needs a reader in
//HeightmapImport.cpp and its extension added there. Rows are read from y = 0, the same order Export writes them
//(the first row of the file, north for maps of the real world).
namespace heightmap {
	struct ImportInfo {
		int bitsPerSample = 0;			//precision stored in the file
		bool convertedFromColor = false;	//a color PNG was turned into greyscale (mean of R, G and B)
		bool floatingPoint = false;		//samples are floating point
		bool metres = false;			//a map of the real world (DEM): values are metres and the value range is not used
		std::optional<glm::dvec2> pixelSize;	//from the file: metres, or degrees when geographic; none = not stored
		bool geographic = false;		//pixels in degrees of longitude and latitude
		double latitude = 0.0;			//of the middle of the file, geographic only
		int noDataFilled = 0;			//missing samples filled from their neighbours (dem::FillNoData)
		//Where the pixels lie (GeoTIFF tags, .hgt name, .asc header + .prj); after the crop and the downsample of the import,
		//of the whole file from Inspect. epsg = 0: coordinates of a system the file does not name
		std::optional<geo::GeoReference> georeference;
	};

	//Part of a file in its pixels
	struct PixelRect {
		int x = 0, y = 0, width = 0, height = 0;
	};

	struct ImportOptions {
		//What the lowest and the highest sample mean, for files without units (PNG, R16); not used by maps of the real world
		ValueRange range = terrainRange;
		//Part of the file to keep; none = all of it
		std::optional<PixelRect> crop;
		//The kept part is reduced by this factor (dem::Downsample)
		int downsample = 1;
		//Coordinate system of a file whose coordinates name none (georeference epsg 0); does not replace a system the file names
		std::optional<int> epsg;
	};

	struct FileHeader {
		int width = 0, height = 0;
		ImportInfo info;		//noDataFilled is not known before reading
	};

	//Reads only what the file says about itself (size, precision, pixel size), to choose the options before importing
	bool Inspect(const std::filesystem::path& path, FileHeader& out, std::string& error);

	//Reads the file, crops it, fills the missing samples of the kept part and downsamples it; the result must be at most dem::maxMapSize on each side
	//@param out - replaced only on success
	//@param info - details of the file, set on success
	//@param error - description of the problem on failure
	//@return true on success
	bool Import(const std::filesystem::path& path, const ImportOptions& options, HeightField& out, ImportInfo& info, std::string& error);
	//Files do not store the value range, so the caller chooses what the lowest and the highest sample mean.
	//Use the range the map was exported with to get back the same values.
	//@param range - values the sample range is mapped to
	bool Import(const std::filesystem::path& path, ValueRange range, HeightField& out, ImportInfo& info, std::string& error);

	//@return files in folder with a supported extension, sorted by path; empty if the folder does not exist
	std::vector<std::filesystem::path> ListImportable(const std::filesystem::path& folder);

	//The coordinate system of an ESRI .prj (WKT) by its names: UTM zones (WGS84, ETRS89), PUWG 1992 (CS92) and 2000 (CS2000 zones 5–8),
	//a GEOGCS without PROJCS = degrees
	//@return the EPSG code; 0 = none of these
	int SystemFromPrj(const std::string& text);
}
