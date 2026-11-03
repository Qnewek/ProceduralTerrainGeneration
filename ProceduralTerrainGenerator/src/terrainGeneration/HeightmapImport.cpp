#include "HeightmapImport.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <system_error>

#include <lodepng.h>

namespace heightmap {
	namespace {
		constexpr int minSize = 2;
		constexpr int maxSize = 8192;

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
			bytes.resize(static_cast<size_t>(size));
			file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!file) {
				error = "Cannot read the file";
				return false;
			}
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

		bool ReadR16(const std::filesystem::path& path, const std::vector<unsigned char>& file, ValueRange range, HeightField& out, ImportInfo& info, std::string& error)
		{
			if (file.empty() || file.size() % 2 != 0) {
				error = "The file size is not a whole number of 16-bit samples";
				return false;
			}
			const unsigned long long samples = file.size() / 2;

			const std::u8string stemText = path.stem().u8string();
			unsigned long long width = 0, height = 0;
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
	}

	bool Import(const std::filesystem::path& path, ValueRange range, HeightField& out, ImportInfo& info, std::string& error)
	{
		const std::string extension = LowercaseExtension(path);
		if (!IsPng(extension) && !IsR16(extension)) {
			error = "Unsupported file type: " + (extension.empty() ? std::string("no extension") : extension);
			return false;
		}

		std::vector<unsigned char> file;
		if (!ReadFile(path, file, error)) {
			return false;
		}
		if (IsPng(extension)) {
			return ReadPng(file, range, out, info, error);
		}
		return ReadR16(path, file, range, out, info, error);
	}

	std::vector<std::filesystem::path> ListImportable(const std::filesystem::path& folder)
	{
		std::vector<std::filesystem::path> files;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
			const std::string extension = LowercaseExtension(it->path());
			if (it->is_regular_file(ec) && (IsPng(extension) || IsR16(extension))) {
				files.push_back(it->path());
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}
