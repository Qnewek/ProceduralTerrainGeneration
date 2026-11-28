#include "LandCover.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <numeric>

#include "TiffFile.h"

namespace landcover {
	namespace {
		constexpr int nodeSpacing = 16;		//map pixels between the nodes where positions are converted exactly
		constexpr int maxSamples = 4;		//k at most: k x k samples per map pixel

		std::string NameOf(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		//The most frequent code of a few, 0 skipped; a tie goes to the smaller code. @param codes - sorted
		uint8_t MostFrequent(const uint8_t* codes, int count)
		{
			uint8_t best = 0;
			int bestCount = 0;
			for (int i = 0; i < count;) {
				int j = i;
				while (j < count && codes[j] == codes[i]) {
					j++;
				}
				//Sorted ascending, so an equal count later is a larger code: only a larger count wins
				if (codes[i] != 0 && j - i > bestCount) {
					best = codes[i];
					bestCount = j - i;
				}
				i = j;
			}
			return best;
		}

		//Rows of a part of a TIFF, decoding each tile (or strip) row once while the rows go down
		class RowReader {
		public:
			RowReader(TIFF* _tif, uint32_t _width, uint32_t _height) : tif(_tif), width(_width), height(_height)
			{
				tiled = TIFFIsTiled(tif) != 0;
				if (tiled) {
					TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileWidth);
					TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileHeight);
				}
				else {
					uint32_t rows = height;
					TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rows);
					tileWidth = width;
					tileHeight = std::clamp(rows, 1u, height);
					//A file saved as one strip (or a few huge ones) would be decoded whole: a 40000² file is 1.6 GB. Such strips are read a row
					//at a time instead (libtiff decodes forward through the strip as the rows go down)
					byRow = static_cast<uint64_t>(tileWidth) * tileHeight > largestStrip;
				}
			}

			//Copies row y, columns [x0, x1), to out; @return false on a read error
			bool Row(uint32_t y, uint32_t x0, uint32_t x1, uint8_t* out)
			{
				if (tileWidth == 0 || tileHeight == 0 || x0 >= x1 || x1 > width || y >= height) {
					return false;
				}
				if (byRow) {
					//Compressed strips have no random access: rows are decoded in order from the start of the strip (or from the last row read)
					if (line.empty() || y < nextRow || y / tileHeight != (nextRow - 1) / tileHeight) {
						const tmsize_t rawScan = TIFFScanlineSize(tif);
						const uint64_t scanSize = std::max<uint64_t>(rawScan > 0 ? static_cast<uint64_t>(TIFFScanlineSize64(tif)) : 0, width);
						if (scanSize == 0 || scanSize > maxTileBytes) {
							line.clear();
							return false;
						}
						line.assign(static_cast<size_t>(scanSize), 0);
						nextRow = y / tileHeight * tileHeight;
					}
					while (nextRow <= y) {
						if (TIFFReadScanline(tif, line.data(), nextRow, 0) < 0) {
							line.clear();
							return false;
						}
						nextRow++;
					}
					std::copy(line.begin() + x0, line.begin() + x1, out);
					return true;
				}
				if (tiled && (tileWidth > maxTileSide || tileHeight > maxTileSide)) {
					return false;
				}
				const uint32_t band = y / tileHeight;
				const uint32_t reqFirst = x0 / tileWidth;
				const uint32_t reqLast = (x1 - 1) / tileWidth;
				if (band != cachedBand || reqFirst != firstColumn || reqLast != lastColumn || buffers.empty()) {
					if (!DecodeBand(band, x0, x1)) {
						return false;
					}
				}
				const uint32_t inTile = y - band * tileHeight;
				for (uint32_t x = x0; x < x1;) {
					const uint32_t column = x / tileWidth;
					const uint32_t end = std::min(x1, (column + 1) * tileWidth);
					const std::vector<uint8_t>& tile = buffers[column - firstColumn];
					std::copy(tile.begin() + static_cast<ptrdiff_t>(inTile) * tileWidth + (x - column * tileWidth),
						tile.begin() + static_cast<ptrdiff_t>(inTile) * tileWidth + (end - column * tileWidth), out + (x - x0));
					x = end;
				}
				return true;
			}

		private:
			bool DecodeBand(uint32_t band, uint32_t x0, uint32_t x1)
			{
				const uint64_t expected = static_cast<uint64_t>(tileWidth) * tileHeight;
				const tmsize_t rawEncoded = tiled ? TIFFTileSize(tif) : TIFFStripSize(tif);
				const uint64_t encodedSize = rawEncoded > 0 ? static_cast<uint64_t>(tiled ? TIFFTileSize64(tif) : TIFFStripSize64(tif)) : 0;
				if (expected == 0 || expected > maxTileBytes || encodedSize > maxTileBytes) {
					buffers.clear();
					return false;
				}
				firstColumn = x0 / tileWidth;
				lastColumn = (x1 - 1) / tileWidth;
				buffers.resize(lastColumn - firstColumn + 1);
				for (uint32_t column = firstColumn; column <= lastColumn; column++) {
					std::vector<uint8_t>& buffer = buffers[column - firstColumn];
					//A full tile even at the edges: libtiff pads the last tiles and strips
					buffer.assign(static_cast<size_t>(std::max(expected, encodedSize)), 0);
					const tmsize_t read = tiled
						? TIFFReadEncodedTile(tif, TIFFComputeTile(tif, column * tileWidth, band * tileHeight, 0, 0), buffer.data(), static_cast<tmsize_t>(buffer.size()))
						: TIFFReadEncodedStrip(tif, band, buffer.data(), static_cast<tmsize_t>(buffer.size()));
					if (read < 0) {
						buffers.clear();
						return false;
					}
				}
				cachedBand = band;
				return true;
			}

			//Bytes of a strip above which it is read by rows
			static constexpr uint64_t largestStrip = 16u << 20;
			static constexpr uint32_t maxTileSide = 16384;
			static constexpr uint64_t maxTileBytes = uint64_t(64) << 20;

			TIFF* tif;
			uint32_t width, height;
			bool tiled = false, byRow = false;
			uint32_t nextRow = 0;			//by rows: the row after the one in line
			std::vector<uint8_t> line;
			uint32_t tileWidth = 0, tileHeight = 0;
			uint32_t cachedBand = 0, firstColumn = 0, lastColumn = 0;
			std::vector<std::vector<uint8_t>> buffers;
		};

		//The part of a file under the map, in memory
		struct Source {
			tifffile::Handle tif;
			int width = 0, height = 0;					//of the file
			std::optional<geo::GeoReference> reference;
			bool rotated = false;						//a rotated or sheared GeoTIFF: no reference
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;			//part read, in pixels of the file
			int stride = 1;								//file pixels per raster pixel
			int rasterWidth = 0, rasterHeight = 0;
			std::vector<uint8_t> raster;
			std::vector<glm::dvec2> nodes;				//file pixel of each node of the map
		};

		bool Open(const std::filesystem::path& path, Source& source, FileStatus& status)
		{
			std::string error;
			source.tif = tifffile::Open(path, error);
			if (!source.tif) {
				status.error = error;
				return false;
			}
			TIFF* tif = source.tif.get();
			uint32_t width = 0, height = 0;
			uint16_t samples = 1, bits = 1, format = SAMPLEFORMAT_UINT, planar = PLANARCONFIG_CONTIG;
			TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width);
			TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height);
			TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samples);
			TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bits);
			TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &format);
			TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
			if (samples != 1 || bits != 8 || format != SAMPLEFORMAT_UINT || planar != PLANARCONFIG_CONTIG) {
				status.error = "Not a land cover file: one unsigned 8-bit class per pixel expected (the file has " + std::to_string(samples)
					+ " x " + std::to_string(bits) + "-bit)";
				return false;
			}
			if (width < 1 || height < 1 || width > 1000000 || height > 1000000) {
				status.error = "Wrong size " + std::to_string(width) + "x" + std::to_string(height);
				return false;
			}
			source.width = static_cast<int>(width);
			source.height = static_cast<int>(height);
			source.reference = tifffile::Georeference(tif);
			source.rotated = !source.reference && tifffile::Rotated(tif);
			status.system = source.reference ? geo::SystemName(source.reference->epsg) : source.rotated ? "rotated" : "no georeference";
			return true;
		}

		//Reads the part [x0, x1) x [y0, y1) of the file reduced by its stride; @return false on a read error
		bool ReadRaster(Source& source, FileStatus& status)
		{
			const int w = source.x1 - source.x0, h = source.y1 - source.y0, s = source.stride;
			source.rasterWidth = (w + s - 1) / s;
			source.rasterHeight = (h + s - 1) / s;
			source.raster.assign(static_cast<size_t>(source.rasterWidth) * source.rasterHeight, 0);
			RowReader reader(source.tif.get(), static_cast<uint32_t>(source.width), static_cast<uint32_t>(source.height));
			std::vector<uint8_t> band(static_cast<size_t>(w) * s);
			std::array<uint32_t, 256> histogram{};
			std::vector<uint8_t> touched;
			for (int r = 0; r < source.rasterHeight; r++) {
				const int y = source.y0 + r * s, rows = std::min(s, source.y1 - y);
				for (int i = 0; i < rows; i++) {
					if (!reader.Row(static_cast<uint32_t>(y + i), static_cast<uint32_t>(source.x0), static_cast<uint32_t>(source.x1),
						band.data() + static_cast<size_t>(i) * w)) {
						status.error = "Cannot read the file" + tifffile::LastErrorSuffix();
						return false;
					}
				}
				uint8_t* out = source.raster.data() + static_cast<size_t>(r) * source.rasterWidth;
				if (s == 1) {
					std::copy(band.begin(), band.begin() + w, out);
					continue;
				}
				for (int c = 0; c < source.rasterWidth; c++) {
					const int x = c * s, cols = std::min(s, w - x);
					for (int i = 0; i < rows; i++) {
						for (int j = 0; j < cols; j++) {
							const uint8_t code = band[static_cast<size_t>(i) * w + x + j];
							if (code != 0 && histogram[code]++ == 0) {
								touched.push_back(code);
							}
						}
					}
					uint8_t best = 0;
					uint32_t bestCount = 0;
					for (uint8_t code : touched) {
						if (histogram[code] > bestCount || (histogram[code] == bestCount && code < best)) {
							best = code;
							bestCount = histogram[code];
						}
						histogram[code] = 0;
					}
					touched.clear();
					out[c] = best;
				}
			}
			return true;
		}
	}

	ClassMap Compute(const std::vector<std::filesystem::path>& files, int width, int height, const std::optional<geo::GeoReference>& map,
		int maxRasterSide)
	{
		ClassMap result;
		result.width = std::max(width, 0);
		result.height = std::max(height, 0);
		result.classes.assign(static_cast<size_t>(result.width) * result.height, 0);
		result.files.resize(files.size());
		if (maxRasterSide <= 0) {
			maxRasterSide = 2 * std::max(width, height);
		}
		maxRasterSide = std::max(maxRasterSide, 1);

		//Map pixel positions of the nodes; the last node lies on or past the far edge
		const int nodeColumns = (result.width + nodeSpacing - 1) / nodeSpacing + 1, nodeRows = (result.height + nodeSpacing - 1) / nodeSpacing + 1;
		auto nodePixel = [&](int i, int j) { return glm::dvec2(i * nodeSpacing, j * nodeSpacing); };
		const bool placed = map && geo::Supported(map->epsg);
		std::vector<glm::dvec2> nodeDegrees;
		if (placed) {
			nodeDegrees.resize(static_cast<size_t>(nodeColumns) * nodeRows);
			for (int j = 0; j < nodeRows; j++) {
				for (int i = 0; i < nodeColumns; i++) {
					nodeDegrees[static_cast<size_t>(j) * nodeColumns + i] = *geo::PixelToDegrees(*map, nodePixel(i, j));
				}
			}
		}

		std::vector<Source> sources(files.size());
		std::vector<int> used;
		bool stretchedOne = false;
		for (size_t f = 0; f < files.size(); f++) {
			FileStatus& status = result.files[f];
			status.path = NameOf(files[f]);
			Source& source = sources[f];
			if (!Open(files[f], source, status)) {
				continue;
			}
			if (result.classes.empty()) {
				continue;
			}
			//Where the map lies in the file: the file pixel of every node
			source.nodes.resize(static_cast<size_t>(nodeColumns) * nodeRows);
			if (placed) {
				if (!source.reference) {
					status.skipped = source.rotated ? "rotated or sheared GeoTIFF (not supported)" : "no georeference";
					continue;
				}
				if (!geo::Supported(source.reference->epsg)) {
					status.skipped = "coordinate system not supported";
					continue;
				}
				for (size_t n = 0; n < source.nodes.size(); n++) {
					source.nodes[n] = *geo::DegreesToPixel(*source.reference, nodeDegrees[n]);
				}
			}
			else {
				if (stretchedOne) {
					status.skipped = "only the first file is stretched over a map without georeference";
					continue;
				}
				stretchedOne = true;
				status.stretched = true;
				const glm::dvec2 scale(static_cast<double>(source.width) / result.width, static_cast<double>(source.height) / result.height);
				for (int j = 0; j < nodeRows; j++) {
					for (int i = 0; i < nodeColumns; i++) {
						source.nodes[static_cast<size_t>(j) * nodeColumns + i] = nodePixel(i, j) * scale;
					}
				}
			}
			//The part under the map: the nodes on its edge, 2 pixels more on every side
			glm::dvec2 lowest(1e300), highest(-1e300);
			for (int j = 0; j < nodeRows; j++) {
				for (int i = 0; i < nodeColumns; i++) {
					if (i == 0 || j == 0 || i == nodeColumns - 1 || j == nodeRows - 1) {
						const glm::dvec2 p = source.nodes[static_cast<size_t>(j) * nodeColumns + i];
						lowest = glm::min(lowest, p);
						highest = glm::max(highest, p);
					}
				}
			}
			source.x0 = static_cast<int>(std::clamp(std::floor(lowest.x) - 2.0, 0.0, static_cast<double>(source.width)));
			source.y0 = static_cast<int>(std::clamp(std::floor(lowest.y) - 2.0, 0.0, static_cast<double>(source.height)));
			source.x1 = static_cast<int>(std::clamp(std::ceil(highest.x) + 2.0, 0.0, static_cast<double>(source.width)));
			source.y1 = static_cast<int>(std::clamp(std::ceil(highest.y) + 2.0, 0.0, static_cast<double>(source.height)));
			if (source.x1 <= source.x0 || source.y1 <= source.y0) {
				//The file does not reach the map: used, covering nothing
				continue;
			}
			const int longer = std::max(source.x1 - source.x0, source.y1 - source.y0);
			source.stride = (longer + maxRasterSide - 1) / maxRasterSide;
			if (!ReadRaster(source, status)) {
				source.raster = {};
				continue;
			}
			source.tif.reset();
			used.push_back(static_cast<int>(f));
		}
		if (result.classes.empty()) {
			return result;
		}

		//Samples per map pixel side: how many raster pixels a map pixel spans (at the middle of the map), 1 to 4
		int k = 1;
		for (int f : used) {
			const Source& source = sources[f];
			const int i = std::min(nodeColumns - 2, nodeColumns / 2), j = std::min(nodeRows - 2, nodeRows / 2);
			const glm::dvec2 at = source.nodes[static_cast<size_t>(j) * nodeColumns + i];
			const glm::dvec2 right = source.nodes[static_cast<size_t>(j) * nodeColumns + i + 1];
			const glm::dvec2 down = source.nodes[static_cast<size_t>(j + 1) * nodeColumns + i];
			const double span = std::max(glm::length(right - at), glm::length(down - at)) / (nodeSpacing * source.stride);
			k = std::max(k, std::clamp(static_cast<int>(std::ceil(span - 1e-9)), 1, maxSamples));
		}

		//Rows in parallel; every row counts the map pixels each file gave
		std::vector<uint32_t> rowFiles(static_cast<size_t>(result.height) * std::max<size_t>(files.size(), 1), 0);
		std::vector<int> rows(result.height);
		std::iota(rows.begin(), rows.end(), 0);
		std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int y) {
			uint8_t codes[maxSamples * maxSamples];
			int from[maxSamples * maxSamples];
			uint32_t* given = rowFiles.data() + static_cast<size_t>(y) * std::max<size_t>(files.size(), 1);
			for (int x = 0; x < result.width; x++) {
				int count = 0;
				for (int sj = 0; sj < k; sj++) {
					for (int si = 0; si < k; si++) {
						const glm::dvec2 p(x + (si + 0.5) / k, y + (sj + 0.5) / k);
						const int i = std::min(static_cast<int>(p.x) / nodeSpacing, nodeColumns - 2);
						const int j = std::min(static_cast<int>(p.y) / nodeSpacing, nodeRows - 2);
						const glm::dvec2 t = (p - nodePixel(i, j)) / static_cast<double>(nodeSpacing);
						uint8_t code = 0;
						int source = -1;
						for (int f : used) {
							const Source& s = sources[f];
							const glm::dvec2* n = s.nodes.data() + static_cast<size_t>(j) * nodeColumns + i;
							const glm::dvec2 filePixel = glm::mix(glm::mix(n[0], n[1], t.x), glm::mix(n[nodeColumns], n[nodeColumns + 1], t.x), t.y);
							if (filePixel.x < 0.0 || filePixel.y < 0.0 || filePixel.x >= s.width || filePixel.y >= s.height) {
								continue;
							}
							const int rx = (static_cast<int>(filePixel.x) - s.x0) / s.stride, ry = (static_cast<int>(filePixel.y) - s.y0) / s.stride;
							if (rx < 0 || ry < 0 || rx >= s.rasterWidth || ry >= s.rasterHeight) {
								continue;
							}
							code = s.raster[static_cast<size_t>(ry) * s.rasterWidth + rx];
							if (code != 0) {
								source = f;
								break;
							}
						}
						codes[count] = code;
						from[count] = source;
						count++;
					}
				}
				//Sorted with the files alongside, so the first sample of the winning class names its file
				int order[maxSamples * maxSamples];
				std::iota(order, order + count, 0);
				std::stable_sort(order, order + count, [&](int a, int b) { return codes[a] < codes[b]; });
				uint8_t sorted[maxSamples * maxSamples];
				for (int i = 0; i < count; i++) {
					sorted[i] = codes[order[i]];
				}
				const uint8_t best = MostFrequent(sorted, count);
				result.classes[static_cast<size_t>(y) * result.width + x] = best;
				if (best != 0) {
					for (int i = 0; i < count; i++) {
						if (sorted[i] == best) {
							given[from[order[i]]]++;
							break;
						}
					}
				}
			}
		});

		for (uint8_t code : result.classes) {
			result.counts[code]++;
		}
		const double pixels = static_cast<double>(result.classes.size());
		for (size_t f = 0; f < files.size(); f++) {
			uint64_t total = 0;
			for (int y = 0; y < result.height; y++) {
				total += rowFiles[static_cast<size_t>(y) * files.size() + f];
			}
			result.files[f].coverage = static_cast<float>(total / pixels);
		}
		return result;
	}
}
