#include "MaterialExport.h"

#include <algorithm>
#include <cctype>

#include <lodepng.h>

#include "FileIO.h"

namespace splatmap {
	Shares FromLayers(const uint8_t* layers, int width, int height, int layerCount, int materials)
	{
		Shares s;
		s.width = width;
		s.height = height;
		s.channels = std::clamp(materials, 0, 4 * layerCount);
		const size_t texels = static_cast<size_t>(width) * height;
		s.data.resize(texels * s.channels);
		for (size_t t = 0; t < texels; t++) {
			for (int m = 0; m < s.channels; m++) {
				const size_t layer = static_cast<size_t>(m / 4);
				s.data[t * s.channels + m] = layers[(layer * texels + t) * 4 + m % 4];
			}
		}
		return s;
	}

	void Normalize(Shares& shares)
	{
		const int channels = shares.channels;
		if (channels == 0) {
			return;
		}
		const size_t texels = static_cast<size_t>(shares.width) * shares.height;
		std::vector<int> order(channels);
		std::vector<int> remainder(channels);
		for (size_t t = 0; t < texels; t++) {
			uint8_t* texel = &shares.data[t * channels];
			int sum = 0;
			for (int m = 0; m < channels; m++) {
				sum += texel[m];
			}
			if (sum == 0 || sum == 255) {
				continue;
			}
			//Integer parts of value * 255 / sum, then the missing units go to the largest remainders (ties: lower material first)
			int assigned = 0;
			for (int m = 0; m < channels; m++) {
				const int scaled = texel[m] * 255;
				remainder[m] = scaled % sum;
				texel[m] = static_cast<uint8_t>(scaled / sum);
				assigned += texel[m];
				order[m] = m;
			}
			std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return remainder[a] > remainder[b]; });
			for (int i = 0; assigned < 255; i++, assigned++) {
				texel[order[i]]++;
			}
		}
	}

	std::vector<uint8_t> Channel(const Shares& shares, int material)
	{
		const size_t texels = static_cast<size_t>(shares.width) * shares.height;
		std::vector<uint8_t> out(texels);
		for (size_t t = 0; t < texels; t++) {
			out[t] = shares.data[t * shares.channels + material];
		}
		return out;
	}

	std::vector<uint8_t> Rgba(const Shares& shares, int firstMaterial)
	{
		const size_t texels = static_cast<size_t>(shares.width) * shares.height;
		std::vector<uint8_t> out(texels * 4, 0);
		for (size_t t = 0; t < texels; t++) {
			for (int c = 0; c < 4; c++) {
				const int m = firstMaterial + c;
				if (m < shares.channels) {
					out[t * 4 + c] = shares.data[t * shares.channels + m];
				}
			}
		}
		return out;
	}

	std::vector<float> Coverage(const Shares& shares)
	{
		std::vector<double> sums(shares.channels, 0.0);
		const size_t texels = static_cast<size_t>(shares.width) * shares.height;
		for (size_t t = 0; t < texels; t++) {
			for (int m = 0; m < shares.channels; m++) {
				sums[m] += shares.data[t * shares.channels + m];
			}
		}
		std::vector<float> coverage(shares.channels, 0.0f);
		if (texels > 0) {
			for (int m = 0; m < shares.channels; m++) {
				coverage[m] = static_cast<float>(sums[m] / (255.0 * static_cast<double>(texels)));
			}
		}
		return coverage;
	}

	std::vector<uint8_t> BiomeIds(const BiomeMap& ids)
	{
		std::vector<uint8_t> out(ids.Size());
		for (size_t i = 0; i < ids.Size(); i++) {
			out[i] = static_cast<uint8_t>(std::clamp(ids[i], 0, 255));
		}
		return out;
	}

	std::vector<float> BiomeCoverage(const BiomeMap& ids, int count)
	{
		std::vector<float> coverage(std::max(count, 0), 0.0f);
		if (ids.Empty()) {
			return coverage;
		}
		std::vector<size_t> counts(coverage.size(), 0);
		for (size_t i = 0; i < ids.Size(); i++) {
			if (ids[i] >= 0 && ids[i] < count) {
				counts[ids[i]]++;
			}
		}
		for (size_t b = 0; b < counts.size(); b++) {
			coverage[b] = static_cast<float>(static_cast<double>(counts[b]) / static_cast<double>(ids.Size()));
		}
		return coverage;
	}

	std::vector<uint8_t> Colors(const float* rgba, size_t texels)
	{
		std::vector<uint8_t> out(texels * 3);
		for (size_t t = 0; t < texels; t++) {
			for (int c = 0; c < 3; c++) {
				const float v = rgba[t * 4 + c];
				//NaN ends up at 0
				out[t * 3 + c] = static_cast<uint8_t>(v > 0.0f ? std::min(v, 1.0f) * 255.0f + 0.5f : 0.0f);
			}
		}
		return out;
	}

	std::string SafeName(const std::string& name)
	{
		std::string out;
		for (char c : name) {
			const unsigned char u = static_cast<unsigned char>(c);
			out += std::isalnum(u) && u < 128 ? static_cast<char>(std::tolower(u)) : '_';
		}
		return out;
	}

	bool WritePng8(const std::filesystem::path& path, const std::vector<uint8_t>& pixels, int width, int height, int channels, std::string& error)
	{
		LodePNGColorType type;
		switch (channels) {
		case 1: type = LCT_GREY; break;
		case 3: type = LCT_RGB; break;
		case 4: type = LCT_RGBA; break;
		default:
			error = "Unsupported number of channels: " + std::to_string(channels);
			return false;
		}
		if (width <= 0 || height <= 0 || pixels.size() != static_cast<size_t>(width) * height * channels) {
			error = "The map is empty or has a wrong size";
			return false;
		}

		lodepng::State state;
		state.info_raw.colortype = type;
		state.info_raw.bitdepth = 8;
		state.info_png.color.colortype = type;
		state.info_png.color.bitdepth = 8;
		//Weights must stay as they are: no palette or dropped channels for maps with few distinct values
		state.encoder.auto_convert = 0;
		std::vector<unsigned char> png;
		const unsigned result = lodepng::encode(png, pixels.data(), static_cast<unsigned>(width), static_cast<unsigned>(height), state);
		if (result != 0) {
			error = std::string("PNG encoding failed: ") + lodepng_error_text(result);
			return false;
		}
		return WriteFileAtomic(path, png.data(), png.size(), error);
	}
}
