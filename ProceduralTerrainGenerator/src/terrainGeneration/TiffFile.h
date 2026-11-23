#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <tiffio.h>

#include "GeoReference.h"

//Reading TIFF files with libtiff, shared by the height map import and the land cover: opening with the error of libtiff, array tags
//libtiff does not know (GeoTIFF) and the georeference. Only for .cpp files of ptg_core (libtiff is linked privately)
namespace tifffile {
	struct Closer {
		void operator()(TIFF* tif) const { TIFFClose(tif); }
	};
	using Handle = std::unique_ptr<TIFF, Closer>;

	//@param error - "Not a valid TIFF" with the message of libtiff, on failure
	Handle Open(const std::filesystem::path& path, std::string& error);
	//": " + the last error of libtiff on this thread, empty when there was none
	std::string LastErrorSuffix();

	//An array tag, whose count libtiff passes as uint16 or uint32 depending on how the field is defined (GeoTIFF tags it does not know are
	//read as anonymous fields)
	template<typename T>
	bool GetArray(TIFF* tif, uint32_t tag, TIFFDataType type, std::vector<T>& out)
	{
		const TIFFField* field = TIFFFindField(tif, tag, TIFF_ANY);
		if (!field || TIFFFieldDataType(field) != type || !TIFFFieldPassCount(field)) {
			return false;
		}
		const T* data = nullptr;
		if (TIFFFieldReadCount(field) == TIFF_VARIABLE2) {
			uint32_t count = 0;
			if (!TIFFGetField(tif, tag, &count, &data) || !data) {
				return false;
			}
			out.assign(data, data + count);
		}
		else {
			uint16_t count = 0;
			if (!TIFFGetField(tif, tag, &count, &data) || !data) {
				return false;
			}
			out.assign(data, data + count);
		}
		return true;
	}

	//The georeference from the GeoTIFF tags of the file (geo::FromGeoTiffTags); none without them
	std::optional<geo::GeoReference> Georeference(TIFF* tif);
}
