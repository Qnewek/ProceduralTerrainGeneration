#include "HeightmapExport.h"

#include <lodepng.h>

#include "FileIO.h"

namespace heightmap {
	std::string FileName(const std::string& baseName, Format format, int width, int height)
	{
		if (format == Format::PNG16) {
			return baseName + ".png";
		}
		return baseName + "_" + std::to_string(width) + "x" + std::to_string(height) + ".r16";
	}

	std::vector<uint16_t> Quantize(const HeightField& map, ValueRange range, size_t& clippedCells)
	{
		clippedCells = 0;
		std::vector<uint16_t> values(map.Size());
		const double scale = 65535.0 / (static_cast<double>(range.max) - range.min);
		for (size_t i = 0; i < map.Size(); i++) {
			float value = map[i];
			//Written so that NaN, for which every comparison is false, also ends up at min
			if (!(value >= range.min)) {
				value = range.min;
				clippedCells++;
			}
			else if (value > range.max) {
				value = range.max;
				clippedCells++;
			}
			values[i] = static_cast<uint16_t>((static_cast<double>(value) - range.min) * scale + 0.5);
		}
		return values;
	}

	static bool EncodePng(const std::vector<uint16_t>& values, int width, int height, std::vector<unsigned char>& png, std::string& error)
	{
		//PNG stores 16-bit samples as big-endian
		std::vector<unsigned char> bytes(values.size() * 2);
		for (size_t i = 0; i < values.size(); i++) {
			bytes[i * 2] = static_cast<unsigned char>(values[i] >> 8);
			bytes[i * 2 + 1] = static_cast<unsigned char>(values[i] & 0xFF);
		}

		lodepng::State state;
		state.info_raw.colortype = LCT_GREY;
		state.info_raw.bitdepth = 16;
		state.info_png.color.colortype = LCT_GREY;
		state.info_png.color.bitdepth = 16;
		//Otherwise maps with few distinct values could be stored with fewer bits or as a palette
		state.encoder.auto_convert = 0;

		unsigned result = lodepng::encode(png, bytes.data(), static_cast<unsigned>(width), static_cast<unsigned>(height), state);
		if (result != 0) {
			error = std::string("PNG encoding failed: ") + lodepng_error_text(result);
			return false;
		}
		return true;
	}

	bool Export(const std::filesystem::path& path, const HeightField& map, Format format, ValueRange range, size_t& clippedCells, std::string& error)
	{
		clippedCells = 0;
		if (map.Empty()) {
			error = "The map is empty";
			return false;
		}

		const std::vector<uint16_t> values = Quantize(map, range, clippedCells);
		std::vector<unsigned char> file;
		if (format == Format::PNG16) {
			if (!EncodePng(values, map.GetWidth(), map.GetHeight(), file, error)) {
				return false;
			}
		}
		else {
			file.resize(values.size() * 2);
			for (size_t i = 0; i < values.size(); i++) {
				file[i * 2] = static_cast<unsigned char>(values[i] & 0xFF);
				file[i * 2 + 1] = static_cast<unsigned char>(values[i] >> 8);
			}
		}
		return WriteFileAtomic(path, file.data(), file.size(), error);
	}
}
