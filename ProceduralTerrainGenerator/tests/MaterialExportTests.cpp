#include "doctest/doctest.h"

#include <filesystem>
#include <numeric>
#include <vector>

#include <lodepng.h>

#include "MaterialExport.h"

//Splat map as the GPU stores it: layer after layer, RGBA per texel; value of material m at texel t = (t * 7 + m * 3) % 256
static std::vector<uint8_t> GpuLayers(int width, int height, int layers)
{
	const size_t texels = static_cast<size_t>(width) * height;
	std::vector<uint8_t> bytes(texels * layers * 4);
	for (int layer = 0; layer < layers; layer++) {
		for (size_t t = 0; t < texels; t++) {
			for (int c = 0; c < 4; c++) {
				const int m = layer * 4 + c;
				bytes[(layer * texels + t) * 4 + c] = static_cast<uint8_t>((t * 7 + m * 3) % 256);
			}
		}
	}
	return bytes;
}

TEST_CASE("FromLayers puts every material of every layer at its texel")
{
	const int width = 5, height = 3, layers = 4;
	const std::vector<uint8_t> bytes = GpuLayers(width, height, layers);
	const splatmap::Shares s = splatmap::FromLayers(bytes.data(), width, height, layers, 14);
	CHECK(s.channels == 14);
	int wrong = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const size_t t = static_cast<size_t>(y) * width + x;
			for (int m = 0; m < 14; m++) {
				wrong += s.At(x, y, m) != static_cast<uint8_t>((t * 7 + m * 3) % 256);
			}
		}
	}
	CHECK(wrong == 0);
	//More materials than the layers hold are cut to 4 per layer
	CHECK(splatmap::FromLayers(bytes.data(), width, height, 2, 14).channels == 8);
}

static splatmap::Shares OneTexel(std::vector<uint8_t> values)
{
	splatmap::Shares s;
	s.width = 1;
	s.height = 1;
	s.channels = static_cast<int>(values.size());
	s.data = std::move(values);
	return s;
}

static int Sum(const splatmap::Shares& s)
{
	return std::accumulate(s.data.begin(), s.data.end(), 0);
}

TEST_CASE("Normalize makes every texel sum to 255 and keeps the proportions")
{
	SUBCASE("Rounding loss of the GPU (sum 253)")
	{
		splatmap::Shares s = OneTexel({ 85, 84, 84, 0 });
		splatmap::Normalize(s);
		CHECK(Sum(s) == 255);
		CHECK(s.data[0] == 86);
		CHECK(s.data[1] == 85);
		CHECK(s.data[2] == 84);
		CHECK(s.data[3] == 0);
	}
	SUBCASE("Sum above 255")
	{
		splatmap::Shares s = OneTexel({ 128, 128, 1 });
		splatmap::Normalize(s);
		CHECK(Sum(s) == 255);
		CHECK(s.data[0] == 127);
		CHECK(s.data[1] == 127);
		CHECK(s.data[2] == 1);
	}
	SUBCASE("A single material gets everything, an empty texel stays empty, a correct one is kept")
	{
		splatmap::Shares single = OneTexel({ 0, 3, 0 });
		splatmap::Normalize(single);
		CHECK(single.data == std::vector<uint8_t>{ 0, 255, 0 });

		splatmap::Shares empty = OneTexel({ 0, 0 });
		splatmap::Normalize(empty);
		CHECK(Sum(empty) == 0);

		splatmap::Shares correct = OneTexel({ 200, 55 });
		splatmap::Normalize(correct);
		CHECK(correct.data == std::vector<uint8_t>{ 200, 55 });
	}
	SUBCASE("Every texel of a map")
	{
		const std::vector<uint8_t> bytes = GpuLayers(6, 4, 4);
		splatmap::Shares s = splatmap::FromLayers(bytes.data(), 6, 4, 4, 16);
		splatmap::Normalize(s);
		int wrong = 0;
		for (int t = 0; t < 24; t++) {
			int sum = 0;
			for (int m = 0; m < 16; m++) {
				sum += s.data[t * 16 + m];
			}
			wrong += sum != 255;
		}
		CHECK(wrong == 0);
	}
}

TEST_CASE("Channel, Rgba and Coverage")
{
	splatmap::Shares s;
	s.width = 2;
	s.height = 1;
	s.channels = 5;
	s.data = { 255, 0, 0, 0, 0,
		0, 0, 0, 51, 204 };

	CHECK(splatmap::Channel(s, 4) == std::vector<uint8_t>{ 0, 204 });
	CHECK(splatmap::Rgba(s, 0) == std::vector<uint8_t>{ 255, 0, 0, 0, 0, 0, 0, 51 });
	//Channels past the last material are 0
	CHECK(splatmap::Rgba(s, 4) == std::vector<uint8_t>{ 0, 0, 0, 0, 204, 0, 0, 0 });

	const std::vector<float> coverage = splatmap::Coverage(s);
	REQUIRE(coverage.size() == 5);
	CHECK(coverage[0] == doctest::Approx(0.5f));
	CHECK(coverage[1] == 0.0f);
	CHECK(coverage[3] == doctest::Approx(0.1f));
	CHECK(coverage[4] == doctest::Approx(0.4f));
}

TEST_CASE("Biome ids, coverage and colors")
{
	BiomeMap ids(4, 1);
	ids[0] = 0;
	ids[1] = 2;
	ids[2] = 2;
	ids[3] = 300;
	CHECK(splatmap::BiomeIds(ids) == std::vector<uint8_t>{ 0, 2, 2, 255 });
	const std::vector<float> coverage = splatmap::BiomeCoverage(ids, 3);
	REQUIRE(coverage.size() == 3);
	CHECK(coverage[0] == doctest::Approx(0.25f));
	CHECK(coverage[1] == 0.0f);
	CHECK(coverage[2] == doctest::Approx(0.5f));

	const float rgba[8] = { 1.0f, 0.5f, 0.0f, 1.0f, 2.0f, -1.0f, 0.2f, 0.0f };
	CHECK(splatmap::Colors(rgba, 2) == std::vector<uint8_t>{ 255, 128, 0, 255, 0, 51 });
}

TEST_CASE("SafeName keeps letters and digits")
{
	CHECK(splatmap::SafeName("Grass 2") == "grass_2");
	CHECK(splatmap::SafeName("Rock/Scree-wet") == "rock_scree_wet");
}

TEST_CASE("WritePng8 writes 8-bit PNGs with the given channels, also with few distinct values")
{
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "ptg_material_export_tests";
	std::filesystem::remove_all(folder);
	std::string error;

	const std::vector<uint8_t> grey = { 0, 0, 0, 0, 0, 255 };
	REQUIRE_MESSAGE(splatmap::WritePng8(folder / "grey.png", grey, 3, 2, 1, error), error);
	std::vector<unsigned char> decoded;
	unsigned w = 0, h = 0;
	lodepng::State state;
	state.decoder.color_convert = 0;
	std::vector<unsigned char> file;
	REQUIRE(lodepng::load_file(file, (folder / "grey.png").string()) == 0);
	REQUIRE(lodepng::decode(decoded, w, h, state, file) == 0);
	CHECK(w == 3);
	CHECK(h == 2);
	CHECK(state.info_png.color.colortype == LCT_GREY);
	CHECK(state.info_png.color.bitdepth == 8);
	CHECK(std::vector<uint8_t>(decoded.begin(), decoded.end()) == grey);

	std::vector<uint8_t> rgba(2 * 2 * 4, 0);
	rgba[0] = 255;
	REQUIRE_MESSAGE(splatmap::WritePng8(folder / "rgba.png", rgba, 2, 2, 4, error), error);
	decoded.clear();
	REQUIRE(lodepng::decode(decoded, w, h, (folder / "rgba.png").string(), LCT_RGBA, 8) == 0);
	CHECK(std::vector<uint8_t>(decoded.begin(), decoded.end()) == rgba);

	CHECK_FALSE(splatmap::WritePng8(folder / "wrong.png", grey, 2, 2, 1, error));
	CHECK_FALSE(splatmap::WritePng8(folder / "wrong.png", grey, 3, 2, 2, error));
	CHECK_FALSE(std::filesystem::exists(folder / "wrong.png"));

	std::filesystem::remove_all(folder);
}
