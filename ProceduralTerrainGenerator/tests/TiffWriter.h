#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <tiffio.h>

#include "doctest/doctest.h"

//Writing test TIFF files (height maps and land cover classes) with GeoTIFF tags
namespace testtiff {
	//Georeferencing of a test GeoTIFF; pixel = 0 writes none
	struct Geo {
		double pixel = 0.0;
		bool geographic = false;	//used when epsg = 0: only GTModelTypeGeoKey (2 = degrees, 1 = projected)
		double leftX = 0.0;			//X (longitude for degrees) of the left edge
		double topY = 0.0;			//Y (latitude for degrees) of the top edge
		int epsg = 0;				//4326 = degrees with GeographicTypeGeoKey, other = ProjectedCSTypeGeoKey; 0 = no system code
		bool pixelIsPoint = false;	//GTRasterTypeGeoKey = PixelIsPoint (leftX, topY = centre of the first pixel); only with epsg
		std::string noData;			//GDAL_NODATA, empty = none
		double rotation = 0.0;		//≠ 0: ModelTransformationTag with this term of X per row (a rotated raster) instead of scale + tiepoint
	};

	struct TiffSpec {
		int width = 0, height = 0;
		uint16_t sampleFormat = SAMPLEFORMAT_INT;
		uint16_t bits = 16;				//8 (unsigned), 16, 32 or 64 (float)
		uint16_t compression = COMPRESSION_NONE;
		uint32_t tile = 0;			//0 = strips
		uint32_t rowsPerStrip = 2;	//with strips; the height = the whole file in one strip
		uint16_t samplesPerPixel = 1;
		Geo geo;
	};

	//Spec of a land cover file: one unsigned 8-bit sample
	inline TiffSpec ClassSpec(int width, int height, const Geo& geo, uint32_t tile = 0)
	{
		TiffSpec spec;
		spec.width = width;
		spec.height = height;
		spec.sampleFormat = SAMPLEFORMAT_UINT;
		spec.bits = 8;
		spec.compression = COMPRESSION_DEFLATE;
		spec.tile = tile;
		spec.geo = geo;
		return spec;
	}

	//Sets an array tag whose count libtiff passes as uint16 (TIFF_VARIABLE) or uint32 (TIFF_VARIABLE2)
	template<typename T>
	void SetArray(TIFF* tif, uint32_t tag, uint32_t count, const T* values)
	{
		const TIFFField* field = TIFFFindField(tif, tag, TIFF_ANY);
		if (field && TIFFFieldReadCount(field) == TIFF_VARIABLE2) {
			TIFFSetField(tif, tag, count, values);
		}
		else {
			TIFFSetField(tif, tag, static_cast<uint16_t>(count), values);
		}
	}

	inline void WriteTiff(const std::filesystem::path& path, const TiffSpec& spec, const std::vector<double>& values)
	{
		TIFF* tif = TIFFOpenW(path.wstring().c_str(), "w");
		REQUIRE(tif);
		static const TIFFFieldInfo geoFields[] = {
			{ 33550, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, const_cast<char*>("ModelPixelScaleTag") },
			{ 33922, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, const_cast<char*>("ModelTiepointTag") },
			{ 34264, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, const_cast<char*>("ModelTransformationTag") },
			{ 34735, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_SHORT, FIELD_CUSTOM, 1, 1, const_cast<char*>("GeoKeyDirectoryTag") },
			{ TIFFTAG_GDAL_NODATA, TIFF_VARIABLE, TIFF_VARIABLE, TIFF_ASCII, FIELD_CUSTOM, 1, 0, const_cast<char*>("GDAL_NODATA") },
		};
		for (const TIFFFieldInfo& info : geoFields) {
			if (!TIFFFindField(tif, info.field_tag, TIFF_ANY)) {
				TIFFMergeFieldInfo(tif, &info, 1);
			}
		}
		TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(spec.width));
		TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(spec.height));
		TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, spec.samplesPerPixel);
		TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, spec.bits);
		TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, spec.sampleFormat);
		TIFFSetField(tif, TIFFTAG_COMPRESSION, spec.compression);
		TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, spec.samplesPerPixel == 3 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
		TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		if (spec.geo.pixel > 0.0) {
			const double scale[3] = { spec.geo.pixel, spec.geo.pixel, 0.0 };
			const double tiepoint[6] = { 0.0, 0.0, 0.0, spec.geo.leftX, spec.geo.topY, 0.0 };
			if (spec.geo.rotation != 0.0) {
				const double matrix[16] = { spec.geo.pixel, spec.geo.rotation, 0.0, spec.geo.leftX, 0.0, -spec.geo.pixel, 0.0, spec.geo.topY,
					0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
				SetArray(tif, 34264, 16, matrix);
			}
			else {
				SetArray(tif, 33550, 3, scale);
				SetArray(tif, 33922, 6, tiepoint);
			}
			if (spec.geo.epsg == 0) {
				const uint16_t keys[8] = { 1, 1, 0, 1, 1024, 0, 1, static_cast<uint16_t>(spec.geo.geographic ? 2 : 1) };
				SetArray(tif, 34735, 8, keys);
			}
			else {
				const bool degrees = spec.geo.epsg == 4326;
				const uint16_t keys[16] = { 1, 1, 0, 3, 1024, 0, 1, static_cast<uint16_t>(degrees ? 2 : 1),
					1025, 0, 1, static_cast<uint16_t>(spec.geo.pixelIsPoint ? 2 : 1),
					static_cast<uint16_t>(degrees ? 2048 : 3072), 0, 1, static_cast<uint16_t>(spec.geo.epsg) };
				SetArray(tif, 34735, 16, keys);
			}
		}
		if (!spec.geo.noData.empty()) {
			TIFFSetField(tif, TIFFTAG_GDAL_NODATA, spec.geo.noData.c_str());
		}

		const int bytes = spec.bits / 8;
		auto put = [&](unsigned char* to, double v) {
			if (spec.sampleFormat == SAMPLEFORMAT_IEEEFP && spec.bits == 32) { const float f = static_cast<float>(v); std::memcpy(to, &f, 4); }
			else if (spec.sampleFormat == SAMPLEFORMAT_IEEEFP) { std::memcpy(to, &v, 8); }
			else if (spec.bits == 8) { *to = static_cast<unsigned char>(v); }
			else if (spec.bits == 16) { const int16_t s = static_cast<int16_t>(v); std::memcpy(to, &s, 2); }
			else { const int32_t s = static_cast<int32_t>(v); std::memcpy(to, &s, 4); }
		};
		const int pixelBytes = bytes * spec.samplesPerPixel;
		if (spec.tile == 0) {
			TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, spec.rowsPerStrip);
			std::vector<unsigned char> row(static_cast<size_t>(spec.width) * pixelBytes);
			for (int y = 0; y < spec.height; y++) {
				for (int x = 0; x < spec.width; x++) {
					for (int s = 0; s < spec.samplesPerPixel; s++) {
						put(&row[static_cast<size_t>(x) * pixelBytes + s * bytes], values[static_cast<size_t>(y) * spec.width + x]);
					}
				}
				REQUIRE(TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), 0) == 1);
			}
		}
		else {
			TIFFSetField(tif, TIFFTAG_TILEWIDTH, spec.tile);
			TIFFSetField(tif, TIFFTAG_TILELENGTH, spec.tile);
			std::vector<unsigned char> tile(static_cast<size_t>(spec.tile) * spec.tile * pixelBytes, 0);
			for (uint32_t ty = 0; ty < static_cast<uint32_t>(spec.height); ty += spec.tile) {
				for (uint32_t tx = 0; tx < static_cast<uint32_t>(spec.width); tx += spec.tile) {
					std::fill(tile.begin(), tile.end(), static_cast<unsigned char>(0));
					for (uint32_t y = 0; y < spec.tile && ty + y < static_cast<uint32_t>(spec.height); y++) {
						for (uint32_t x = 0; x < spec.tile && tx + x < static_cast<uint32_t>(spec.width); x++) {
							put(&tile[(static_cast<size_t>(y) * spec.tile + x) * pixelBytes], values[static_cast<size_t>(ty + y) * spec.width + tx + x]);
						}
					}
					REQUIRE(TIFFWriteTile(tif, tile.data(), tx, ty, 0, 0) > 0);
				}
			}
		}
		TIFFClose(tif);
	}

	inline std::filesystem::path TiffFolder()
	{
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_import_tiff_tests";
		std::filesystem::create_directories(folder);
		return folder;
	}
}
