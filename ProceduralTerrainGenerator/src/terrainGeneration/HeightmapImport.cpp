#include "HeightmapImport.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>

#include <lodepng.h>
#include <tiffio.h>

#include "DemTools.h"
#include "TiffFile.h"

namespace heightmap {
	namespace {
		constexpr int minSize = 2;
		constexpr int maxSize = dem::maxMapSize;
		//Files larger than a map are read whole before cropping and downsampling, up to these limits (2^28 floats = 1 GB)
		constexpr unsigned long long maxSourceSide = 32768, maxSourceSamples = 1ull << 28;
		constexpr uintmax_t maxImportFileBytes = uintmax_t(2) << 30;
		constexpr uint64_t maxTiffBufferBytes = uint64_t(256) << 20;
		//No height on Earth is this far from 0: such samples are NoData markers (often ±FLT_MAX, written without a NoData tag)
		constexpr double hugeSample = 1e30;

		std::string SizeLimitMessage(unsigned long long width, unsigned long long height)
		{
			return "Map size must be between " + std::to_string(minSize) + " and " + std::to_string(maxSize)
				+ " (is " + std::to_string(width) + "x" + std::to_string(height) + ")";
		}

		bool SizeAllowed(unsigned long long width, unsigned long long height)
		{
			return width >= minSize && width <= maxSize && height >= minSize && height <= maxSize;
		}

		bool ReadFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes, std::string& error)
		{
			std::error_code ec;
			const uintmax_t size = std::filesystem::file_size(path, ec);
			std::ifstream file(path, std::ios::binary);
			if (ec || !file) {
				error = "Cannot open the file";
				return false;
			}
			if (size > maxImportFileBytes || size > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
				error = "The file is too large to read";
				return false;
			}
			bytes.resize(static_cast<size_t>(size));
			if (size > 0) {
				file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				if (!file) {
					error = "Cannot read the file";
					return false;
				}
			}
			return true;
		}

		const char* SkipSpace(const char* p, const char* end)
		{
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
				p++;
			}
			return p;
		}

		bool ParseDouble(const char*& p, const char* end, double& value)
		{
			p = SkipSpace(p, end);
			if (p >= end) {
				return false;
			}
			const char* start = *p == '+' ? p + 1 : p;
			if (*p == '+' && (start >= end || *start == '+' || *start == '-')) {
				return false;
			}
			const std::from_chars_result r = std::from_chars(start, end, value);
			if (r.ec != std::errc() || r.ptr == start) {
				return false;
			}
			p = r.ptr;
			return true;
		}

		//Inverse of Quantize
		float SampleToValue(uint16_t sample, ValueRange range)
		{
			return static_cast<float>(range.min + sample / 65535.0 * (static_cast<double>(range.max) - range.min));
		}

		bool ReadPng(const std::vector<unsigned char>& file, ValueRange range, HeightField& out, ImportInfo& info, std::string& error)
		{
			unsigned width = 0, height = 0;
			lodepng::State header;
			unsigned result = lodepng_inspect(&width, &height, &header, file.data(), file.size());
			if (result != 0) {
				error = std::string("Not a valid PNG: ") + lodepng_error_text(result);
				return false;
			}
			if (!SizeAllowed(width, height)) {
				error = SizeLimitMessage(width, height);
				return false;
			}

			const LodePNGColorType sourceType = header.info_png.color.colortype;
			const unsigned sourceBits = header.info_png.color.bitdepth;

			//lodepng does not convert color to greyscale and converts to 16 bits only from 16 bits,
			//so every file is decoded to RGBA of its own precision and turned into samples here
			const bool wide = sourceBits == 16;
			lodepng::State state;
			state.info_raw.colortype = LCT_RGBA;
			state.info_raw.bitdepth = wide ? 16 : 8;
			std::vector<unsigned char> pixels;
			result = lodepng::decode(pixels, width, height, state, file);
			if (result != 0) {
				error = std::string("PNG decoding failed: ") + lodepng_error_text(result);
				return false;
			}

			HeightField map(static_cast<int>(width), static_cast<int>(height));
			for (size_t i = 0; i < map.Size(); i++) {
				uint32_t sample = 0;
				if (wide) {
					const unsigned char* p = &pixels[i * 8];
					sample = (((p[0] << 8) | p[1]) + ((p[2] << 8) | p[3]) + ((p[4] << 8) | p[5])) / 3u;
				}
				else {
					const unsigned char* p = &pixels[i * 4];
					//257 maps the 8-bit range exactly onto the 16-bit range: 255 becomes 65535
					sample = (p[0] + p[1] + p[2]) / 3u * 257u;
				}
				map[i] = SampleToValue(static_cast<uint16_t>(sample), range);
			}

			info.bitsPerSample = static_cast<int>(sourceBits);
			info.convertedFromColor = sourceType == LCT_RGB || sourceType == LCT_RGBA || sourceType == LCT_PALETTE;
			out = std::move(map);
			return true;
		}

		//Reads the size written by Export at the end of a file name, e.g. "alps_1025x513"
		bool SizeFromName(const std::string& stem, unsigned long long& width, unsigned long long& height)
		{
			const size_t underscore = stem.rfind('_');
			const size_t separator = stem.rfind('x');
			if (underscore == std::string::npos || separator == std::string::npos || separator < underscore) {
				return false;
			}
			const char* first = stem.data() + underscore + 1;
			const char* middle = stem.data() + separator;
			const char* last = stem.data() + stem.size();
			std::from_chars_result w = std::from_chars(first, middle, width);
			std::from_chars_result h = std::from_chars(middle + 1, last, height);
			return w.ec == std::errc() && w.ptr == middle && h.ec == std::errc() && h.ptr == last;
		}

		//Size of an R16 file from the size in its name or, without one, as a square
		bool R16Size(const std::filesystem::path& path, uintmax_t fileSize, unsigned long long& width, unsigned long long& height, std::string& error)
		{
			if (fileSize == 0 || fileSize % 2 != 0) {
				error = "The file size is not a whole number of 16-bit samples";
				return false;
			}
			const unsigned long long samples = fileSize / 2;

			const std::u8string stemText = path.stem().u8string();
			if (SizeFromName(std::string(stemText.begin(), stemText.end()), width, height)) {
				if (!SizeAllowed(width, height)) {
					error = SizeLimitMessage(width, height);
					return false;
				}
				if (width * height != samples) {
					error = "The file does not match the size in its name (" + std::to_string(width) + "x" + std::to_string(height) + ")";
					return false;
				}
			}
			else {
				//Without a size in the name only a square map can be recognized
				width = height = static_cast<unsigned long long>(std::llround(std::sqrt(static_cast<double>(samples))));
				if (width * height != samples) {
					error = "The file is not square; add the size to its name, e.g. map_1024x512.r16";
					return false;
				}
				if (!SizeAllowed(width, height)) {
					error = SizeLimitMessage(width, height);
					return false;
				}
			}
			return true;
		}

		bool ReadR16(const std::filesystem::path& path, const std::vector<unsigned char>& file, ValueRange range, HeightField& out, ImportInfo& info, std::string& error)
		{
			unsigned long long width = 0, height = 0;
			if (!R16Size(path, file.size(), width, height, error)) {
				return false;
			}

			HeightField map(static_cast<int>(width), static_cast<int>(height));
			for (size_t i = 0; i < map.Size(); i++) {
				const uint16_t sample = static_cast<uint16_t>(file[i * 2] | (file[i * 2 + 1] << 8));
				map[i] = SampleToValue(sample, range);
			}

			info.bitsPerSample = 16;
			info.convertedFromColor = false;
			out = std::move(map);
			return true;
		}

		std::string LowercaseExtension(const std::filesystem::path& path)
		{
			const std::u8string text = path.extension().u8string();
			std::string extension(text.begin(), text.end());
			std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) {
				return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
			});
			return extension;
		}

		bool IsPng(const std::string& extension) { return extension == ".png"; }
		bool IsR16(const std::string& extension) { return extension == ".r16" || extension == ".raw"; }
		bool IsTiff(const std::string& extension) { return extension == ".tif" || extension == ".tiff"; }
		bool IsHgt(const std::string& extension) { return extension == ".hgt"; }
		bool IsAsc(const std::string& extension) { return extension == ".asc"; }
		bool Supported(const std::string& extension)
		{
			return IsPng(extension) || IsR16(extension) || IsTiff(extension) || IsHgt(extension) || IsAsc(extension);
		}

		std::string UnsupportedMessage(const std::string& extension)
		{
			return "Unsupported file type: " + (extension.empty() ? std::string("no extension") : extension);
		}

		bool SourceAllowed(unsigned long long width, unsigned long long height, std::string& error)
		{
			if (width < minSize || height < minSize || width > maxSourceSide || height > maxSourceSide || width * height > maxSourceSamples) {
				error = "The file is too large to read (" + std::to_string(width) + "x" + std::to_string(height) + "; at most "
					+ std::to_string(maxSourceSide) + " on a side and " + std::to_string(maxSourceSamples) + " samples)";
				return false;
			}
			return true;
		}

		//--- GeoTIFF (libtiff) ---
		using TiffHandle = tifffile::Handle;
		using tifffile::GetArray;

		TiffHandle OpenTiff(const std::filesystem::path& path, std::string& error) { return tifffile::Open(path, error); }

		std::optional<double> NoDataOf(TIFF* tif)
		{
			const TIFFField* field = TIFFFindField(tif, TIFFTAG_GDAL_NODATA, TIFF_ANY);
			if (!field) {
				return std::nullopt;
			}
			//libtiff does not know the tag: the file's copy is read as an anonymous field, passing its count first
			const char* text = nullptr;
			int found = 0;
			if (!TIFFFieldPassCount(field)) {
				found = TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &text);
			}
			else if (TIFFFieldReadCount(field) == TIFF_VARIABLE2) {
				uint32_t count = 0;
				found = TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &count, &text);
			}
			else {
				uint16_t count = 0;
				found = TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &count, &text);
			}
			if (!found || !text) {
				return std::nullopt;
			}
			const char* p = text;
			double value = 0.0;
			return ParseDouble(p, text + std::strlen(text), value) && std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
		}

		struct TiffLayout {
			uint32_t width = 0, height = 0;
			uint16_t bits = 0, format = SAMPLEFORMAT_UINT;
		};

		bool ReadTiffHeader(TIFF* tif, TiffLayout& layout, FileHeader& header, std::string& error)
		{
			uint16_t samples = 1, planar = PLANARCONFIG_CONTIG;
			TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &layout.width);
			TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &layout.height);
			TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples);
			TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &layout.bits);
			TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &layout.format);
			TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
			if (samples != 1) {
				error = "Only height maps with one sample per pixel are supported (the file has " + std::to_string(samples) + ")";
				return false;
			}
			const bool integer = (layout.format == SAMPLEFORMAT_INT || layout.format == SAMPLEFORMAT_UINT)
				&& (layout.bits == 8 || layout.bits == 16 || layout.bits == 32);
			const bool floating = layout.format == SAMPLEFORMAT_IEEEFP && (layout.bits == 32 || layout.bits == 64);
			if (!integer && !floating) {
				error = "Unsupported TIFF samples: " + std::to_string(layout.bits) + "-bit, format " + std::to_string(layout.format);
				return false;
			}
			if (!SourceAllowed(layout.width, layout.height, error)) {
				return false;
			}
			header.width = static_cast<int>(layout.width);
			header.height = static_cast<int>(layout.height);
			ImportInfo& info = header.info;
			info.bitsPerSample = layout.bits;
			info.floatingPoint = floating;
			//Samples of a TIFF height map are taken as metres (GeoTIFF DEMs store them so)
			info.metres = true;

			std::vector<double> scale, tiepoint, transformation;
			std::vector<uint16_t> keys;
			if (GetArray(tif, 33550, TIFF_DOUBLE, scale) && scale.size() >= 2 && scale[0] > 0.0 && scale[1] > 0.0) {
				info.pixelSize = glm::dvec2(scale[0], scale[1]);
			}
			GetArray(tif, 33922, TIFF_DOUBLE, tiepoint);
			GetArray(tif, 34264, TIFF_DOUBLE, transformation);
			//GeoKeyDirectory: 4 header shorts, then {key, location, count, value}; GTModelTypeGeoKey (1024) 2 = geographic
			if (GetArray(tif, 34735, TIFF_SHORT, keys)) {
				for (size_t k = 4; k + 3 < keys.size(); k += 4) {
					if (keys[k] == 1024 && keys[k + 1] == 0) {
						info.geographic = keys[k + 3] == 2;
					}
				}
			}
			//Tiepoint (I, J, K, X, Y, Z): raster point (I, J) lies at Y; rows go south
			if (info.geographic && info.pixelSize && tiepoint.size() >= 6) {
				info.latitude = tiepoint[4] - (header.height / 2.0 - tiepoint[1]) * info.pixelSize->y;
			}
			info.georeference = geo::FromGeoTiffTags(scale, tiepoint, transformation, keys);
			info.rotated = geo::RotatedTransformation(transformation);
			return true;
		}

		double SampleAt(const unsigned char* p, const TiffLayout& layout)
		{
			if (layout.format == SAMPLEFORMAT_IEEEFP) {
				if (layout.bits == 32) { float v; std::memcpy(&v, p, 4); return v; }
				double v; std::memcpy(&v, p, 8); return v;
			}
			const bool sign = layout.format == SAMPLEFORMAT_INT;
			switch (layout.bits) {
			case 8: return sign ? static_cast<double>(static_cast<int8_t>(*p)) : static_cast<double>(*p);
			case 16: { uint16_t v; std::memcpy(&v, p, 2); return sign ? static_cast<double>(static_cast<int16_t>(v)) : static_cast<double>(v); }
			default: { uint32_t v; std::memcpy(&v, p, 4); return sign ? static_cast<double>(static_cast<int32_t>(v)) : static_cast<double>(v); }
			}
		}

		bool ReadTiff(const std::filesystem::path& path, HeightField& out, std::vector<uint8_t>& missing, ImportInfo& info, std::string& error)
		{
			TiffHandle tif = OpenTiff(path, error);
			if (!tif) {
				return false;
			}
			TiffLayout layout;
			FileHeader header;
			if (!ReadTiffHeader(tif.get(), layout, header, error)) {
				return false;
			}
			const std::optional<double> noData = NoDataOf(tif.get());
			const float noDataValue = noData ? static_cast<float>(*noData) : 0.0f;
			HeightField map(header.width, header.height);
			missing.assign(map.Size(), 0);
			const int bytes = layout.bits / 8;
			auto store = [&](uint32_t x, uint32_t y, const unsigned char* p) {
				const float v = static_cast<float>(SampleAt(p, layout));
				const size_t i = static_cast<size_t>(y) * header.width + x;
				missing[i] = !std::isfinite(v) || std::abs(v) >= hugeSample || (noData && v == noDataValue);
				map[i] = missing[i] ? 0.0f : v;
			};
			if (TIFFIsTiled(tif.get())) {
				uint32_t tileWidth = 0, tileHeight = 0;
				TIFFGetField(tif.get(), TIFFTAG_TILEWIDTH, &tileWidth);
				TIFFGetField(tif.get(), TIFFTAG_TILELENGTH, &tileHeight);
				if (tileWidth == 0 || tileHeight == 0 || tileWidth > maxSourceSide || tileHeight > maxSourceSide) {
					error = "Invalid TIFF tile size";
					return false;
				}
				const uint64_t expected = static_cast<uint64_t>(tileWidth) * tileHeight * static_cast<uint64_t>(bytes);
				const tmsize_t rawTileSize = TIFFTileSize(tif.get());
				const uint64_t tileSize = rawTileSize > 0 ? static_cast<uint64_t>(TIFFTileSize64(tif.get())) : 0;
				if (tileSize == 0 || tileSize < expected || tileSize > maxTiffBufferBytes) {
					error = "Invalid TIFF tile buffer size";
					return false;
				}
				std::vector<unsigned char> tile(static_cast<size_t>(tileSize));
				for (uint32_t ty = 0; ty < layout.height; ty += tileHeight) {
					for (uint32_t tx = 0; tx < layout.width; tx += tileWidth) {
						if (TIFFReadTile(tif.get(), tile.data(), tx, ty, 0, 0) < 0) {
							error = "Cannot read a tile of the TIFF" + tifffile::LastErrorSuffix();
							return false;
						}
						for (uint32_t y = 0; y < tileHeight && ty + y < layout.height; y++) {
							for (uint32_t x = 0; x < tileWidth && tx + x < layout.width; x++) {
								store(tx + x, ty + y, &tile[(static_cast<size_t>(y) * tileWidth + x) * bytes]);
							}
						}
					}
				}
			}
			else {
				const uint64_t expected = static_cast<uint64_t>(layout.width) * static_cast<uint64_t>(bytes);
				const tmsize_t rawRowSize = TIFFScanlineSize(tif.get());
				const uint64_t rowSize = rawRowSize > 0 ? static_cast<uint64_t>(TIFFScanlineSize64(tif.get())) : 0;
				if (rowSize == 0 || rowSize < expected || rowSize > maxTiffBufferBytes) {
					error = "Invalid TIFF scanline buffer size";
					return false;
				}
				std::vector<unsigned char> row(static_cast<size_t>(rowSize));
				for (uint32_t y = 0; y < layout.height; y++) {
					if (TIFFReadScanline(tif.get(), row.data(), y, 0) < 0) {
						error = "Cannot read a row of the TIFF" + tifffile::LastErrorSuffix();
						return false;
					}
					for (uint32_t x = 0; x < layout.width; x++) {
						store(x, y, &row[static_cast<size_t>(x) * bytes]);
					}
				}
			}
			info = header.info;
			out = std::move(map);
			return true;
		}

		//--- SRTM .hgt: big-endian int16 metres, rows from north, 1201² (3") or 3601² (1"), named after its south-west corner (N50E019) ---

		constexpr int16_t hgtVoid = -32768;

		bool HgtHeader(const std::filesystem::path& path, uintmax_t fileSize, FileHeader& header, std::string& error)
		{
			int side = 0;
			for (int candidate : { 1201, 3601 }) {
				if (fileSize == static_cast<uintmax_t>(candidate) * candidate * 2) {
					side = candidate;
				}
			}
			if (side == 0) {
				error = "An SRTM .hgt tile has 1201x1201 or 3601x3601 samples (the file has " + std::to_string(fileSize) + " bytes)";
				return false;
			}
			header.width = header.height = side;
			ImportInfo& info = header.info;
			info.bitsPerSample = 16;
			info.metres = true;
			info.geographic = true;
			info.pixelSize = glm::dvec2(1.0 / (side - 1));
			//Latitude of the middle from the name: the south-west corner plus half a degree; an unusual name leaves 0
			const std::u8string stemText = path.stem().u8string();
			const std::string stem(stemText.begin(), stemText.end());
			if (stem.size() >= 3 && (stem[0] == 'N' || stem[0] == 'n' || stem[0] == 'S' || stem[0] == 's')) {
				int corner = 0;
				const std::from_chars_result r = std::from_chars(stem.data() + 1, stem.data() + stem.size(), corner);
				if (r.ec == std::errc() && corner >= 0 && corner < 90) {
					const int south = stem[0] == 'S' || stem[0] == 's' ? -corner : corner;
					info.latitude = south + 0.5;
					//Longitude after the latitude: E019 / W020; samples lie on whole degrees, so the corner of pixel (0, 0) is half a pixel out
					const char* rest = r.ptr;
					const char* end = stem.data() + stem.size();
					int lon = 0;
					if (rest < end && (*rest == 'E' || *rest == 'e' || *rest == 'W' || *rest == 'w')) {
						const std::from_chars_result l = std::from_chars(rest + 1, end, lon);
						if (l.ec == std::errc() && lon >= 0 && lon <= 180) {
							const double west = *rest == 'W' || *rest == 'w' ? -lon : lon;
							const double pixel = 1.0 / (side - 1);
							info.georeference = geo::GeoReference{ geo::degreesEpsg, { west - pixel / 2.0, south + 1.0 + pixel / 2.0 }, { pixel, -pixel } };
						}
					}
				}
			}
			return true;
		}

		bool ReadHgt(const std::filesystem::path& path, const std::vector<unsigned char>& file, HeightField& out, std::vector<uint8_t>& missing, ImportInfo& info,
			std::string& error)
		{
			FileHeader header;
			if (!HgtHeader(path, file.size(), header, error)) {
				return false;
			}
			HeightField map(header.width, header.height);
			missing.assign(map.Size(), 0);
			for (size_t i = 0; i < map.Size(); i++) {
				const int16_t sample = static_cast<int16_t>((file[i * 2] << 8) | file[i * 2 + 1]);
				missing[i] = sample == hgtVoid;
				map[i] = missing[i] ? 0.0f : static_cast<float>(sample);
			}
			info = header.info;
			out = std::move(map);
			return true;
		}

		//--- ESRI ASCII grid: "key value" header lines, then nrows rows of ncols values from north ---

		struct AscHeader {
			long long cols = 0, rows = 0;
			double x = 0.0, y = 0.0, cell = 0.0;
			bool center = false;
			std::optional<double> noData;
		};

		//@return the first character of the values, nullptr on an invalid header
		const char* ParseAscHeader(const char* p, const char* end, AscHeader& h, std::string& error)
		{
			bool cols = false, rows = false, x = false, y = false, cell = false;
			while (true) {
				p = SkipSpace(p, end);
				if (p >= end || !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) {
					break;
				}
				std::string key;
				while (p < end && *p != ' ' && *p != '\t') {
					key += static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
					p++;
				}
				double value = 0.0;
				if (!ParseDouble(p, end, value) || !std::isfinite(value)) {
					error = "Invalid ASCII grid header at \"" + key + "\"";
					return nullptr;
				}
				if (key == "ncols" || key == "nrows") {
					if (value < -1e15 || value > 1e15 || std::floor(value) != value) {
						error = "Invalid ASCII grid header at \"" + key + "\"";
						return nullptr;
					}
					if (key == "ncols") { h.cols = static_cast<long long>(value); cols = true; }
					else { h.rows = static_cast<long long>(value); rows = true; }
				}
				else if (key == "xllcorner" || key == "xllcenter") { h.x = value; x = true; h.center = h.center || key == "xllcenter"; }
				else if (key == "yllcorner" || key == "yllcenter") { h.y = value; y = true; h.center = h.center || key == "yllcenter"; }
				else if (key == "cellsize") { h.cell = value; cell = true; }
				else if (key == "nodata_value") { h.noData = value; }
			}
			if (!cols || !rows || !x || !y || !cell || !std::isfinite(h.x) || !std::isfinite(h.y) || !std::isfinite(h.cell) || h.cell <= 0.0) {
				error = "The ASCII grid header needs ncols, nrows, xllcorner (or xllcenter), yllcorner (or yllcenter) and a positive cellsize";
				return nullptr;
			}
			return p;
		}

		bool AscFileHeader(const AscHeader& h, FileHeader& header, std::string& error)
		{
			if (h.cols < 0 || h.rows < 0 || !SourceAllowed(static_cast<unsigned long long>(h.cols), static_cast<unsigned long long>(h.rows), error)) {
				if (error.empty()) {
					error = "Invalid ASCII grid size";
				}
				return false;
			}
			header.width = static_cast<int>(h.cols);
			header.height = static_cast<int>(h.rows);
			ImportInfo& info = header.info;
			info.metres = true;
			info.floatingPoint = true;
			info.pixelSize = glm::dvec2(h.cell);
			//Grids in degrees are not marked as such: cells below 0.02 (1 arc-minute grids included) with the whole grid in longitude and latitude are taken for degrees
			const double cornerX = h.center ? h.x - h.cell / 2.0 : h.x, cornerY = h.center ? h.y - h.cell / 2.0 : h.y;
			info.geographic = h.cell < 0.02 && cornerX >= -180.0 - h.cell && cornerX + h.cols * h.cell <= 180.0 + h.cell
				&& cornerY >= -90.0 - h.cell && cornerY + h.rows * h.cell <= 90.0 + h.cell;
			if (info.geographic) {
				info.latitude = cornerY + h.rows * h.cell / 2.0;
			}
			//The system: degrees by the rule above, otherwise unknown until a .prj beside the file names it (ApplyPrj)
			info.georeference = geo::GeoReference{ info.geographic ? geo::degreesEpsg : 0, { cornerX, cornerY + h.rows * h.cell }, { h.cell, -h.cell } };
			return true;
		}

		//The system of an ASCII grid from <name>.prj beside it, when there is one (it replaces the guess of AscFileHeader)
		void ApplyPrj(const std::filesystem::path& path, ImportInfo& info)
		{
			std::filesystem::path prj = path;
			prj.replace_extension(".prj");
			std::ifstream file(prj, std::ios::binary);
			if (!file || !info.georeference) {
				return;
			}
			std::string text(64 * 1024, '\0');
			file.read(text.data(), static_cast<std::streamsize>(text.size()));
			text.resize(static_cast<size_t>(file.gcount()));
			info.georeference->epsg = SystemFromPrj(text);
		}

		bool ReadAsc(const std::vector<unsigned char>& file, HeightField& out, std::vector<uint8_t>& missing, ImportInfo& info, std::string& error)
		{
			const char* p = reinterpret_cast<const char*>(file.data());
			const char* end = p + file.size();
			AscHeader h;
			p = ParseAscHeader(p, end, h, error);
			FileHeader header;
			if (!p || !AscFileHeader(h, header, error)) {
				return false;
			}
			HeightField map(header.width, header.height);
			missing.assign(map.Size(), 0);
			const char* v = p;
			for (size_t i = 0; i < map.Size(); i++) {
				double value = 0.0;
				if (!ParseDouble(v, end, value)) {
					error = "The ASCII grid has " + std::to_string(i) + " values, expected " + std::to_string(map.Size());
					return false;
				}
				missing[i] = (h.noData && value == *h.noData) || !std::isfinite(value) || std::abs(value) >= hugeSample;
				map[i] = missing[i] ? 0.0f : static_cast<float>(value);
			}
			info = header.info;
			out = std::move(map);
			return true;
		}

		//The part kept, its missing samples and the size of the map, the same for every format.
		//Cropping first: holes are filled only where they are kept, and the rest of a large file costs no time
		bool Finish(HeightField& map, std::vector<uint8_t>& missing, const ImportOptions& options, ImportInfo& info, std::string& error)
		{
			if (options.crop) {
				const PixelRect& c = *options.crop;
				const long long right = static_cast<long long>(c.x) + c.width, bottom = static_cast<long long>(c.y) + c.height;
				if (c.x < 0 || c.y < 0 || c.width < 1 || c.height < 1 || right > map.GetWidth() || bottom > map.GetHeight()) {
					error = "The crop " + std::to_string(c.width) + "x" + std::to_string(c.height) + " at (" + std::to_string(c.x) + ", " + std::to_string(c.y)
						+ ") lies outside the file (" + std::to_string(map.GetWidth()) + "x" + std::to_string(map.GetHeight()) + ")";
					return false;
				}
				if (c.width != map.GetWidth() || c.height != map.GetHeight()) {
					if (!missing.empty()) {
						std::vector<uint8_t> kept(static_cast<size_t>(c.width) * c.height);
						for (int y = 0; y < c.height; y++) {
							const auto row = missing.begin() + (static_cast<ptrdiff_t>(c.y) + y) * map.GetWidth() + c.x;
							std::copy(row, row + c.width, kept.begin() + static_cast<ptrdiff_t>(y) * c.width);
						}
						missing = std::move(kept);
					}
					map = dem::Crop(map, glm::ivec2(c.x, c.y), glm::ivec2(c.width, c.height));
				}
			}
			info.noDataFilled = 0;
			if (std::find(missing.begin(), missing.end(), uint8_t(1)) != missing.end()) {
				info.noDataFilled = dem::FillNoData(map, missing);
				if (info.noDataFilled < 0) {
					error = "The kept part has no valid samples (all of them are NoData)";
					return false;
				}
			}
			missing = {};
			if (options.downsample > 1) {
				map = dem::Downsample(map, options.downsample);
			}
			if (!SizeAllowed(static_cast<unsigned long long>(map.GetWidth()), static_cast<unsigned long long>(map.GetHeight()))) {
				error = SizeLimitMessage(static_cast<unsigned long long>(map.GetWidth()), static_cast<unsigned long long>(map.GetHeight()))
					+ ": choose an area or a downsample";
				return false;
			}
			if (info.georeference) {
				info.georeference = geo::Cropped(*info.georeference, options.crop ? options.crop->x : 0, options.crop ? options.crop->y : 0, options.downsample);
			}
			return true;
		}
	}

	bool Inspect(const std::filesystem::path& path, FileHeader& out, std::string& error)
	{
		const std::string extension = LowercaseExtension(path);
		FileHeader header;
		if (IsTiff(extension)) {
			TiffHandle tif = OpenTiff(path, error);
			TiffLayout layout;
			if (!tif || !ReadTiffHeader(tif.get(), layout, header, error)) {
				return false;
			}
		}
		else if (IsHgt(extension)) {
			std::error_code ec;
			const uintmax_t size = std::filesystem::file_size(path, ec);
			if (ec) {
				error = "Cannot open the file";
				return false;
			}
			if (!HgtHeader(path, size, header, error)) {
				return false;
			}
		}
		else if (IsAsc(extension)) {
			//The header is a few short lines at the start
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				error = "Cannot open the file";
				return false;
			}
			std::string start(4096, '\0');
			file.read(start.data(), static_cast<std::streamsize>(start.size()));
			start.resize(static_cast<size_t>(file.gcount()));
			AscHeader h;
			if (!ParseAscHeader(start.data(), start.data() + start.size(), h, error) || !AscFileHeader(h, header, error)) {
				return false;
			}
			ApplyPrj(path, header.info);
		}
		else if (IsPng(extension)) {
			//The signature and the IHDR chunk come first
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				error = "Cannot open the file";
				return false;
			}
			std::vector<unsigned char> start(64, 0);
			file.read(reinterpret_cast<char*>(start.data()), static_cast<std::streamsize>(start.size()));
			start.resize(static_cast<size_t>(file.gcount()));
			unsigned width = 0, height = 0;
			lodepng::State state;
			const unsigned result = lodepng_inspect(&width, &height, &state, start.data(), start.size());
			if (result != 0) {
				error = std::string("Not a valid PNG: ") + lodepng_error_text(result);
				return false;
			}
			if (!SizeAllowed(width, height)) {
				error = SizeLimitMessage(width, height);
				return false;
			}
			const LodePNGColorType type = state.info_png.color.colortype;
			header.width = static_cast<int>(width);
			header.height = static_cast<int>(height);
			header.info.bitsPerSample = static_cast<int>(state.info_png.color.bitdepth);
			header.info.convertedFromColor = type == LCT_RGB || type == LCT_RGBA || type == LCT_PALETTE;
		}
		else if (IsR16(extension)) {
			std::error_code ec;
			const uintmax_t size = std::filesystem::file_size(path, ec);
			unsigned long long width = 0, height = 0;
			if (ec) {
				error = "Cannot open the file";
				return false;
			}
			if (!R16Size(path, size, width, height, error)) {
				return false;
			}
			header.width = static_cast<int>(width);
			header.height = static_cast<int>(height);
			header.info.bitsPerSample = 16;
		}
		else {
			error = UnsupportedMessage(extension);
			return false;
		}
		out = header;
		return true;
	}

	bool Import(const std::filesystem::path& path, const ImportOptions& options, HeightField& out, ImportInfo& info, std::string& error)
	{
		const std::string extension = LowercaseExtension(path);
		if (!Supported(extension)) {
			error = UnsupportedMessage(extension);
			return false;
		}
		HeightField map;
		ImportInfo read;
		std::vector<uint8_t> missing;
		bool ok = false;
		if (IsTiff(extension)) {
			ok = ReadTiff(path, map, missing, read, error);
		}
		else {
			std::vector<unsigned char> file;
			if (!ReadFile(path, file, error)) {
				return false;
			}
			if (IsHgt(extension)) {
				ok = ReadHgt(path, file, map, missing, read, error);
			}
			else if (IsAsc(extension)) {
				ok = ReadAsc(file, map, missing, read, error);
			}
			else {
				ok = IsPng(extension) ? ReadPng(file, options.range, map, read, error) : ReadR16(path, file, options.range, map, read, error);
			}
		}
		if (!ok || !Finish(map, missing, options, read, error)) {
			return false;
		}
		if (IsAsc(extension)) {
			ApplyPrj(path, read);
		}
		if (options.epsg && read.georeference && read.georeference->epsg == 0) {
			read.georeference->epsg = *options.epsg;
		}
		info = read;
		out = std::move(map);
		return true;
	}

	bool Import(const std::filesystem::path& path, ValueRange range, HeightField& out, ImportInfo& info, std::string& error)
	{
		ImportOptions options;
		options.range = range;
		return Import(path, options, out, info, error);
	}

	std::vector<std::filesystem::path> ListImportable(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			const std::string extension = LowercaseExtension(it->path());
			if (it->is_regular_file(ec) && Supported(extension)) {
				files.push_back(it->path());
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}

namespace heightmap {
	int SystemFromPrj(const std::string& text)
	{
		std::string lower = text;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
		const size_t projcs = lower.find("projcs[");
		if (projcs == std::string::npos) {
			return lower.find("geogcs[") != std::string::npos ? geo::degreesEpsg : 0;
		}
		//The name of the projected system: the first quoted text of PROJCS
		const size_t open = lower.find('"', projcs);
		const size_t close = open == std::string::npos ? std::string::npos : lower.find('"', open + 1);
		if (close == std::string::npos) {
			return 0;
		}
		const std::string name = lower.substr(open + 1, close - open - 1);
		//The first number after a position, skipping what is not a digit
		auto numberAfter = [&](size_t from, size_t& end) {
			while (from < name.size() && (name[from] < '0' || name[from] > '9')) {
				from++;
			}
			int value = -1;
			const std::from_chars_result r = std::from_chars(name.data() + from, name.data() + name.size(), value);
			end = static_cast<size_t>(r.ptr - name.data());
			return r.ec == std::errc() ? value : -1;
		};
		size_t end = 0;
		if (const size_t cs2000 = name.find("cs2000"); cs2000 != std::string::npos) {
			//Zone by its number (5–8) or by its central meridian (15, 18, 21, 24)
			const int n = numberAfter(cs2000 + 6, end);
			if (n >= 5 && n <= 8) {
				return 2171 + n;
			}
			if (n == 15 || n == 18 || n == 21 || n == 24) {
				return 2171 + n / 3;
			}
			return 0;
		}
		if (name.find("cs92") != std::string::npos || name.find("1992") != std::string::npos) {
			return 2180;
		}
		if (const size_t utm = name.find("utm"); utm != std::string::npos) {
			const size_t zone = name.find("zone", utm);
			const int n = zone == std::string::npos ? -1 : numberAfter(zone + 4, end);
			if (n < 1 || n > 60) {
				return 0;
			}
			const bool south = end < name.size() && name[end] == 's';
			if (name.find("etrs") != std::string::npos) {
				return !south && n >= 28 && n <= 38 ? 25800 + n : 0;
			}
			return (south ? 32700 : 32600) + n;
		}
		return 0;
	}
}
