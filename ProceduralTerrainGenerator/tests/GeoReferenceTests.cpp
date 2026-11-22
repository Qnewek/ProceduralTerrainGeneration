#include "doctest/doctest.h"

#include <cmath>
#include <numbers>
#include <vector>

#include "glm/glm.hpp"

#include "GeoReference.h"

namespace {
	//Length of the meridian from the equator to a latitude on the WGS84 ellipsoid, by Simpson's rule: independent of the Krüger series
	double MeridianArc(double latitudeDegrees)
	{
		const double a = 6378137.0, f = 1.0 / 298.257223563, e2 = f * (2.0 - f);
		const double end = latitudeDegrees * std::numbers::pi / 180.0;
		const int steps = 2000;
		const double h = end / steps;
		auto m = [&](double phi) { const double s = std::sin(phi); return a * (1.0 - e2) / std::pow(1.0 - e2 * s * s, 1.5); };
		double sum = m(0.0) + m(end);
		for (int i = 1; i < steps; i++) {
			sum += (i % 2 == 1 ? 4.0 : 2.0) * m(i * h);
		}
		return sum * h / 3.0;
	}

	void CheckRoundTrip(int epsg, glm::dvec2 lonLat)
	{
		const geo::Projection p = *geo::TransverseMercatorOf(epsg);
		const glm::dvec2 back = geo::ToDegrees(p, geo::ToMetres(p, lonLat));
		CHECK(std::abs(back.x - lonLat.x) < 1e-9);
		CHECK(std::abs(back.y - lonLat.y) < 1e-9);
	}
}

TEST_CASE("Central meridian maps to the false easting")
{
	const std::optional<geo::Projection> p = geo::TransverseMercatorOf(32633);
	REQUIRE(p.has_value());
	const glm::dvec2 m = geo::ToMetres(*p, { 15.0, 45.0 });
	CHECK(std::abs(m.x - 500000.0) < 1e-6);
	CHECK(std::abs(m.y - 0.9996 * 4984944.378) < 0.01);
	//The arc of the test agrees with the published one
	CHECK(std::abs(MeridianArc(45.0) - 4984944.378) < 0.01);
}

TEST_CASE("Equator 3 degrees from the meridian")
{
	const glm::dvec2 m = geo::ToMetres(*geo::TransverseMercatorOf(32631), { 6.0, 0.0 });
	CHECK(std::abs(m.x - 833978.556) < 0.05);
	CHECK(std::abs(m.y) < 1e-6);
}

TEST_CASE("UTM north round trip")
{
	CheckRoundTrip(32634, { 21.3, 52.1 });
	CheckRoundTrip(32634, { 18.0, 49.0 });
	CheckRoundTrip(32634, { 24.0, 54.8 });
}

TEST_CASE("UTM south round trip")
{
	CheckRoundTrip(32723, { -45.2, -23.5 });
	const glm::dvec2 m = geo::ToMetres(*geo::TransverseMercatorOf(32723), { -45.2, -23.5 });
	CHECK(m.y > 5000000.0);
	CHECK(m.y < 10000000.0);
}

TEST_CASE("Symmetric about the meridian")
{
	const geo::Projection p = *geo::TransverseMercatorOf(2180);
	const glm::dvec2 west = geo::ToMetres(p, { 17.0, 51.0 }), east = geo::ToMetres(p, { 21.0, 51.0 });
	CHECK(std::abs((west.x - 500000.0) + (east.x - 500000.0)) < 1e-6);
	CHECK(std::abs(west.y - east.y) < 1e-6);
	CHECK(east.x > 500000.0);
}

TEST_CASE("PUWG 1992 north of the false northing")
{
	const glm::dvec2 m = geo::ToMetres(*geo::TransverseMercatorOf(2180), { 19.0, 52.0 });
	CHECK(std::abs(m.x - 500000.0) < 1e-6);
	CHECK(std::abs(m.y - (0.9993 * MeridianArc(52.0) - 5300000.0)) < 0.01);
}

TEST_CASE("Supported systems")
{
	for (int epsg : { 4326, 2180, 2177, 32634, 32760, 25834 }) {
		CHECK(geo::Supported(epsg));
	}
	for (int epsg : { 0, 3857, 3035, 32661, 25827 }) {
		CHECK_FALSE(geo::Supported(epsg));
	}
	CHECK(geo::SystemName(32634) == "UTM 34N (EPSG:32634)");
	CHECK(geo::SystemName(32734) == "UTM 34S (EPSG:32734)");
	CHECK(geo::SystemName(25834) == "ETRS89 / UTM 34N (EPSG:25834)");
	CHECK(geo::SystemName(2180) == "PUWG 1992 (EPSG:2180)");
	CHECK(geo::SystemName(2178) == "PUWG 2000 zone 7 (EPSG:2178)");
	CHECK(geo::SystemName(4326) == "Degrees (EPSG:4326)");
	CHECK(geo::SystemName(0) == "Unknown system");
	CHECK(geo::SystemName(3035) == "EPSG:3035 (not supported)");
	const std::vector<int> choosable = geo::ChoosableSystems();
	CHECK(choosable.front() == 4326);
	CHECK(choosable.size() == 1 + 1 + 4 + 60 + 60 + 11);
}

TEST_CASE("Pixel and degrees of a georeference")
{
	const geo::GeoReference degrees{ 4326, { 19.0, 51.0 }, { 0.001, -0.001 } };
	const std::optional<glm::dvec2> d = geo::PixelToDegrees(degrees, { 0.5, 0.5 });
	REQUIRE(d.has_value());
	CHECK(std::abs(d->x - 19.0005) < 1e-12);
	CHECK(std::abs(d->y - 50.9995) < 1e-12);
	const std::optional<glm::dvec2> pixel = geo::DegreesToPixel(degrees, *d);
	REQUIRE(pixel.has_value());
	CHECK(std::abs(pixel->x - 0.5) < 1e-9);
	CHECK(std::abs(pixel->y - 0.5) < 1e-9);

	const geo::GeoReference puwg{ 2180, { 500000.0, 300000.0 }, { 30.0, -30.0 } };
	const std::optional<glm::dvec2> corner = geo::PixelToDegrees(puwg, { 0.0, 0.0 });
	REQUIRE(corner.has_value());
	const std::optional<glm::dvec2> back = geo::DegreesToPixel(puwg, *corner);
	REQUIRE(back.has_value());
	CHECK(std::abs(back->x) < 1e-6);
	CHECK(std::abs(back->y) < 1e-6);

	CHECK_FALSE(geo::PixelToDegrees(geo::GeoReference{ 0, { 0.0, 0.0 }, { 1.0, -1.0 } }, { 0.0, 0.0 }).has_value());
	CHECK_FALSE(geo::DegreesToPixel(geo::GeoReference{ 3035, { 0.0, 0.0 }, { 1.0, -1.0 } }, { 0.0, 0.0 }).has_value());
}

TEST_CASE("Cropped moves the corner")
{
	const geo::GeoReference cropped = geo::Cropped({ 32634, { 400000.0, 5800000.0 }, { 30.0, -30.0 } }, 10, 4, 2);
	CHECK(cropped.epsg == 32634);
	CHECK(cropped.origin.x == 400300.0);
	CHECK(cropped.origin.y == 5799880.0);
	CHECK(cropped.pixel.x == 60.0);
	CHECK(cropped.pixel.y == -60.0);
}

TEST_CASE("GeoTIFF tags")
{
	const std::vector<double> scale = { 30.0, 30.0, 0.0 }, tiepoint = { 0.0, 0.0, 0.0, 400000.0, 5800000.0, 0.0 }, none;
	const std::vector<uint16_t> keys = { 1, 1, 0, 3, 1024, 0, 1, 1, 1025, 0, 1, 1, 3072, 0, 1, 32634 };
	const std::optional<geo::GeoReference> g = geo::FromGeoTiffTags(scale, tiepoint, none, keys);
	REQUIRE(g.has_value());
	CHECK(g->epsg == 32634);
	CHECK(g->origin == glm::dvec2(400000.0, 5800000.0));
	CHECK(g->pixel == glm::dvec2(30.0, -30.0));

	std::vector<uint16_t> point = keys;
	point[11] = 2;
	const std::optional<geo::GeoReference> p = geo::FromGeoTiffTags(scale, tiepoint, none, point);
	REQUIRE(p.has_value());
	CHECK(p->origin == glm::dvec2(399985.0, 5800015.0));

	const std::vector<uint16_t> geographic = { 1, 1, 0, 2, 1024, 0, 1, 2, 2048, 0, 1, 4326 };
	const std::optional<geo::GeoReference> d = geo::FromGeoTiffTags({ 0.001, 0.001, 0.0 }, { 0.0, 0.0, 0.0, 19.0, 51.0, 0.0 }, none, geographic);
	REQUIRE(d.has_value());
	CHECK(d->epsg == 4326);

	const std::vector<uint16_t> projectedOnly = { 1, 1, 0, 1, 1024, 0, 1, 1 };
	const std::optional<geo::GeoReference> unknown = geo::FromGeoTiffTags(scale, tiepoint, none, projectedOnly);
	REQUIRE(unknown.has_value());
	CHECK(unknown->epsg == 0);

	std::vector<double> transformation(16, 0.0);
	transformation[0] = 30.0;
	transformation[3] = 400000.0;
	transformation[5] = -30.0;
	transformation[7] = 5800000.0;
	transformation[15] = 1.0;
	const std::optional<geo::GeoReference> t = geo::FromGeoTiffTags(none, none, transformation, keys);
	REQUIRE(t.has_value());
	CHECK(t->origin == glm::dvec2(400000.0, 5800000.0));
	CHECK(t->pixel == glm::dvec2(30.0, -30.0));
	transformation[1] = 0.5;
	CHECK_FALSE(geo::FromGeoTiffTags(none, none, transformation, keys).has_value());
	CHECK_FALSE(geo::FromGeoTiffTags(none, tiepoint, none, keys).has_value());
}
