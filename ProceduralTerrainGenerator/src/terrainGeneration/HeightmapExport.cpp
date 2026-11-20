#include "HeightmapExport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <lodepng.h>

#include "FileIO.h"

namespace heightmap {
	std::string FileName(const std::string& baseName, Format format, int width, int height)
	{
		switch (format) {
		case Format::PNG16: return baseName + ".png";
		case Format::EXR32: return baseName + ".exr";
		case Format::R16:
		default: return baseName + "_" + std::to_string(width) + "x" + std::to_string(height) + ".r16";
		}
	}

	ValueRange MinMax(const HeightField& map)
	{
		bool any = false;
		ValueRange range{ 0.0f, 0.0f };
		for (size_t i = 0; i < map.Size(); i++) {
			const float value = map[i];
			if (!std::isfinite(value)) {
				continue;
			}
			if (!any) {
				range = { value, value };
				any = true;
			}
			range.min = std::min(range.min, value);
			range.max = std::max(range.max, value);
		}
		return range;
	}

	//Little-endian writers for the EXR header and data
	namespace {
		void PutBytes(std::vector<unsigned char>& out, const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			out.insert(out.end(), bytes, bytes + size);
		}
		void PutU8(std::vector<unsigned char>& out, uint8_t value) { out.push_back(value); }
		void PutI32(std::vector<unsigned char>& out, int32_t value)
		{
			const uint32_t v = static_cast<uint32_t>(value);
			for (int i = 0; i < 4; i++) {
				out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
			}
		}
		void PutU64(std::vector<unsigned char>& out, uint64_t value)
		{
			for (int i = 0; i < 8; i++) {
				out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
			}
		}
		void PutF32(std::vector<unsigned char>& out, float value)
		{
			uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			PutI32(out, static_cast<int32_t>(bits));
		}
		void PutString(std::vector<unsigned char>& out, const char* text) { PutBytes(out, text, std::strlen(text) + 1); }
		//Attribute header: name, type name and the size of the value that follows
		void PutAttribute(std::vector<unsigned char>& out, const char* name, const char* type, int32_t size)
		{
			PutString(out, name);
			PutString(out, type);
			PutI32(out, size);
		}
		void PutBox(std::vector<unsigned char>& out, const char* name, int width, int height)
		{
			PutAttribute(out, name, "box2i", 16);
			PutI32(out, 0);
			PutI32(out, 0);
			PutI32(out, width - 1);
			PutI32(out, height - 1);
		}
	}

	std::vector<unsigned char> EncodeExr(const HeightField& map, float scale, ValueRange range, size_t& invalidCells)
	{
		invalidCells = 0;
		const int width = map.GetWidth(), height = map.GetHeight();
		std::vector<unsigned char> out;
		out.reserve(512 + static_cast<size_t>(height) * 16 + map.Size() * 4);

		//Magic number and version 2: a single-part scanline file
		PutI32(out, 20000630);
		PutI32(out, 2);

		//Channel list: "Y", FLOAT (2), not perceptually linear, 3 reserved bytes, sampling 1 x 1, then the list terminator
		PutAttribute(out, "channels", "chlist", 2 + 16 + 1);
		PutString(out, "Y");
		PutI32(out, 2);
		PutU8(out, 0);
		PutU8(out, 0);
		PutU8(out, 0);
		PutU8(out, 0);
		PutI32(out, 1);
		PutI32(out, 1);
		PutU8(out, 0);

		PutAttribute(out, "compression", "compression", 1);
		PutU8(out, 0);	//NO_COMPRESSION: one scanline per block
		PutBox(out, "dataWindow", width, height);
		PutBox(out, "displayWindow", width, height);
		PutAttribute(out, "lineOrder", "lineOrder", 1);
		PutU8(out, 0);	//INCREASING_Y: row 0 first, as in the other formats
		PutAttribute(out, "pixelAspectRatio", "float", 4);
		PutF32(out, 1.0f);
		PutAttribute(out, "screenWindowCenter", "v2f", 8);
		PutF32(out, 0.0f);
		PutF32(out, 0.0f);
		PutAttribute(out, "screenWindowWidth", "float", 4);
		PutF32(out, 1.0f);
		PutU8(out, 0);	//end of the header

		//Offset table: absolute file position of every scanline block (y, data size, data)
		const uint64_t blockSize = 8 + static_cast<uint64_t>(width) * 4;
		const uint64_t firstBlock = out.size() + static_cast<uint64_t>(height) * 8;
		for (int y = 0; y < height; y++) {
			PutU64(out, firstBlock + static_cast<uint64_t>(y) * blockSize);
		}
		for (int y = 0; y < height; y++) {
			PutI32(out, y);
			PutI32(out, width * 4);
			for (int x = 0; x < width; x++) {
				float value = map.At(x, y);
				if (!std::isfinite(value)) {
					value = range.min;
					invalidCells++;
				}
				PutF32(out, value * scale);
			}
		}
		return out;
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

	bool Export(const std::filesystem::path& path, const HeightField& map, Format format, ValueRange range, size_t& clippedCells, std::string& error,
		float exrScale)
	{
		clippedCells = 0;
		if (map.Empty()) {
			error = "The map is empty";
			return false;
		}
		if (format == Format::EXR32) {
			const std::vector<unsigned char> file = EncodeExr(map, exrScale, range, clippedCells);
			return WriteFileAtomic(path, file.data(), file.size(), error);
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
