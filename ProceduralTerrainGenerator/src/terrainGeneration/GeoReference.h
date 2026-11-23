#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "glm/glm.hpp"

//Where a map of the real world lies (Imported map, stage D3): its coordinate system and the position of its pixels, and the systems the
//application converts between: degrees of longitude and latitude (EPSG:4326) and the transverse Mercator projections of UTM and of the
//Polish PUWG 1992 / 2000 grids. One ellipsoid for all (WGS84 = GRS80 well below a metre); no datum shifts
namespace geo {
	inline constexpr int degreesEpsg = 4326;

	//Parameters of a transverse Mercator zone
	struct Projection {
		double lon0Degrees = 0.0;	//central meridian
		double k0 = 1.0;			//scale on the central meridian
		double falseEasting = 0.0, falseNorthing = 0.0;
	};

	//@return the projection of a supported EPSG code; none for degrees, 0 and codes that are not supported
	std::optional<Projection> TransverseMercatorOf(int epsg);
	//@return true for degrees and the supported transverse Mercator codes
	bool Supported(int epsg);
	//Name for the UI, e.g. "UTM 34N (EPSG:32634)"
	std::string SystemName(int epsg);
	//Systems the UI offers for a map whose file does not name one: degrees first
	std::vector<int> ChoosableSystems();

	//@param lonLat - longitude (x) and latitude (y) in degrees; @return easting (x) and northing (y) in metres
	glm::dvec2 ToMetres(const Projection& projection, glm::dvec2 lonLat);
	//@param eastNorth - easting (x) and northing (y) in metres; @return longitude (x) and latitude (y) in degrees
	glm::dvec2 ToDegrees(const Projection& projection, glm::dvec2 eastNorth);

	//Pixels of a raster in a coordinate system: pixel (i, j) covers origin + (i, j) * pixel to origin + (i + 1, j + 1) * pixel
	struct GeoReference {
		int epsg = 0;					//0 = coordinates of a system the file does not name
		glm::dvec2 origin{ 0.0 };		//top left corner of pixel (0, 0), in the units of the system (degrees or metres)
		glm::dvec2 pixel{ 1.0, -1.0 };	//size of a pixel; y < 0 when rows go south
	};

	//Pixel positions are continuous: the centre of pixel i is i + 0.5
	glm::dvec2 PixelToSystem(const GeoReference& reference, glm::dvec2 pixel);
	glm::dvec2 SystemToPixel(const GeoReference& reference, glm::dvec2 point);
	//@return none when the system is not supported
	std::optional<glm::dvec2> PixelToDegrees(const GeoReference& reference, glm::dvec2 pixel);
	std::optional<glm::dvec2> DegreesToPixel(const GeoReference& reference, glm::dvec2 lonLat);

	//The georeference of a part of a raster from pixel (x, y), reduced downsample times
	GeoReference Cropped(const GeoReference& reference, int x, int y, int downsample);

	//The georeference from the GeoTIFF tags (empty vector = tag not in the file): ModelPixelScale (33550), ModelTiepoint (33922),
	//ModelTransformation (34264) and the GeoKeyDirectory (34735)
	//@return none without a pixel size or with a rotation
	std::optional<GeoReference> FromGeoTiffTags(const std::vector<double>& scale, const std::vector<double>& tiepoint,
		const std::vector<double>& transformation, const std::vector<uint16_t>& keys);
}
