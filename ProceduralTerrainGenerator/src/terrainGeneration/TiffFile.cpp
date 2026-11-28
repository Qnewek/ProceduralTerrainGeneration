#include "TiffFile.h"

#include <cstdarg>
#include <cstdio>

namespace tifffile {
	namespace {
		//libtiff reports errors through a global handler: the message of the last error on this thread
		thread_local std::string tiffError;

		void ErrorHandler(const char* module, const char* format, va_list args)
		{
			char text[512];
			std::vsnprintf(text, sizeof(text), format, args);
			tiffError = module ? std::string(module) + ": " + text : std::string(text);
		}
	}

	Handle Open(const std::filesystem::path& path, std::string& error)
	{
		static const bool handlers = [] {
			TIFFSetWarningHandler(nullptr);
			TIFFSetErrorHandler(ErrorHandler);
			return true;
		}();
		(void)handlers;
		tiffError.clear();
#ifdef _WIN32
		Handle tif(TIFFOpenW(path.wstring().c_str(), "r"));
#else
		Handle tif(TIFFOpen(path.c_str(), "r"));
#endif
		if (!tif) {
			error = "Not a valid TIFF" + LastErrorSuffix();
		}
		return tif;
	}

	std::string LastErrorSuffix()
	{
		return tiffError.empty() ? std::string() : ": " + tiffError;
	}

	std::optional<geo::GeoReference> Georeference(TIFF* tif)
	{
		std::vector<double> scale, tiepoint, transformation;
		std::vector<uint16_t> keys;
		GetArray(tif, 33550, TIFF_DOUBLE, scale);
		GetArray(tif, 33922, TIFF_DOUBLE, tiepoint);
		GetArray(tif, 34264, TIFF_DOUBLE, transformation);
		GetArray(tif, 34735, TIFF_SHORT, keys);
		return geo::FromGeoTiffTags(scale, tiepoint, transformation, keys);
	}

	bool Rotated(TIFF* tif)
	{
		std::vector<double> transformation;
		GetArray(tif, 34264, TIFF_DOUBLE, transformation);
		return geo::RotatedTransformation(transformation);
	}
}
