#include "GeoReference.h"

#include <array>
#include <cmath>
#include <numbers>

namespace geo {
	namespace {
		constexpr double a = 6378137.0;
		constexpr double f = 1.0 / 298.257223563;
		constexpr double degrees = 180.0 / std::numbers::pi;

		//Series of Krüger to the sixth order in n (C. F. F. Karney, "Transverse Mercator with an accuracy of a few nanometers", 2011)
		struct Series {
			double e = 0.0;			//eccentricity
			double A = 0.0;			//rectifying radius
			std::array<double, 6> alpha{}, beta{};
		};

		const Series& Krueger()
		{
			static const Series series = [] {
				Series s;
				const double n = f / (2.0 - f), n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;
				s.e = std::sqrt(f * (2.0 - f));
				s.A = a / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0 + n6 / 256.0);
				s.alpha = {
					n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0 + 41.0 * n4 / 180.0 - 127.0 * n5 / 288.0 + 7891.0 * n6 / 37800.0,
					13.0 * n2 / 48.0 - 3.0 * n3 / 5.0 + 557.0 * n4 / 1440.0 + 281.0 * n5 / 630.0 - 1983433.0 * n6 / 1935360.0,
					61.0 * n3 / 240.0 - 103.0 * n4 / 140.0 + 15061.0 * n5 / 26880.0 + 167603.0 * n6 / 181440.0,
					49561.0 * n4 / 161280.0 - 179.0 * n5 / 168.0 + 6601661.0 * n6 / 7257600.0,
					34729.0 * n5 / 80640.0 - 3418889.0 * n6 / 1995840.0,
					212378941.0 * n6 / 319334400.0,
				};
				s.beta = {
					n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0 - n4 / 360.0 - 81.0 * n5 / 512.0 + 96199.0 * n6 / 604800.0,
					n2 / 48.0 + n3 / 15.0 - 437.0 * n4 / 1440.0 + 46.0 * n5 / 105.0 - 1118711.0 * n6 / 3870720.0,
					17.0 * n3 / 480.0 - 37.0 * n4 / 840.0 - 209.0 * n5 / 4480.0 + 5569.0 * n6 / 90720.0,
					4397.0 * n4 / 161280.0 - 11.0 * n5 / 504.0 - 830251.0 * n6 / 7257600.0,
					4583.0 * n5 / 161280.0 - 108847.0 * n6 / 3991680.0,
					20648693.0 * n6 / 638668800.0,
				};
				return s;
			}();
			return series;
		}

		//tan of the conformal latitude from tan of the latitude
		double ConformalTan(double tau, double e)
		{
			const double sigma = std::sinh(e * std::atanh(e * tau / std::sqrt(1.0 + tau * tau)));
			return tau * std::sqrt(1.0 + sigma * sigma) - sigma * std::sqrt(1.0 + tau * tau);
		}

		bool IsUtmNorth(int epsg) { return epsg >= 32601 && epsg <= 32660; }
		bool IsUtmSouth(int epsg) { return epsg >= 32701 && epsg <= 32760; }
		bool IsEtrsUtm(int epsg) { return epsg >= 25828 && epsg <= 25838; }
		bool IsPuwg2000(int epsg) { return epsg >= 2176 && epsg <= 2179; }
	}

	std::optional<Projection> TransverseMercatorOf(int epsg)
	{
		auto utm = [](int zone, bool south) { return Projection{ 6.0 * zone - 183.0, 0.9996, 500000.0, south ? 10000000.0 : 0.0 }; };
		if (IsUtmNorth(epsg)) {
			return utm(epsg - 32600, false);
		}
		if (IsUtmSouth(epsg)) {
			return utm(epsg - 32700, true);
		}
		if (IsEtrsUtm(epsg)) {
			return utm(epsg - 25800, false);
		}
		if (epsg == 2180) {
			return Projection{ 19.0, 0.9993, 500000.0, -5300000.0 };
		}
		if (IsPuwg2000(epsg)) {
			const int zone = epsg - 2171;
			return Projection{ 3.0 * zone, 0.999923, zone * 1000000.0 + 500000.0, 0.0 };
		}
		return std::nullopt;
	}

	bool Supported(int epsg)
	{
		return epsg == degreesEpsg || TransverseMercatorOf(epsg).has_value();
	}

	std::string SystemName(int epsg)
	{
		const std::string code = " (EPSG:" + std::to_string(epsg) + ")";
		if (epsg == degreesEpsg) {
			return "Degrees" + code;
		}
		if (IsUtmNorth(epsg)) {
			return "UTM " + std::to_string(epsg - 32600) + "N" + code;
		}
		if (IsUtmSouth(epsg)) {
			return "UTM " + std::to_string(epsg - 32700) + "S" + code;
		}
		if (IsEtrsUtm(epsg)) {
			return "ETRS89 / UTM " + std::to_string(epsg - 25800) + "N" + code;
		}
		if (epsg == 2180) {
			return "PUWG 1992" + code;
		}
		if (IsPuwg2000(epsg)) {
			return "PUWG 2000 zone " + std::to_string(epsg - 2171) + code;
		}
		if (epsg == 0) {
			return "Unknown system";
		}
		return "EPSG:" + std::to_string(epsg) + " (not supported)";
	}

	std::vector<int> ChoosableSystems()
	{
		std::vector<int> systems = { degreesEpsg, 2180, 2176, 2177, 2178, 2179 };
		for (int zone = 1; zone <= 60; zone++) {
			systems.push_back(32600 + zone);
		}
		for (int zone = 1; zone <= 60; zone++) {
			systems.push_back(32700 + zone);
		}
		for (int zone = 28; zone <= 38; zone++) {
			systems.push_back(25800 + zone);
		}
		return systems;
	}

	glm::dvec2 ToMetres(const Projection& projection, glm::dvec2 lonLat)
	{
		const Series& s = Krueger();
		const double lambda = (lonLat.x - projection.lon0Degrees) / degrees;
		const double tau = std::tan(lonLat.y / degrees);
		const double tauPrime = ConformalTan(tau, s.e);
		const double xiPrime = std::atan2(tauPrime, std::cos(lambda));
		const double etaPrime = std::asinh(std::sin(lambda) / std::sqrt(tauPrime * tauPrime + std::cos(lambda) * std::cos(lambda)));
		double xi = xiPrime, eta = etaPrime;
		for (int j = 1; j <= 6; j++) {
			xi += s.alpha[j - 1] * std::sin(2.0 * j * xiPrime) * std::cosh(2.0 * j * etaPrime);
			eta += s.alpha[j - 1] * std::cos(2.0 * j * xiPrime) * std::sinh(2.0 * j * etaPrime);
		}
		return glm::dvec2(projection.falseEasting + projection.k0 * s.A * eta, projection.falseNorthing + projection.k0 * s.A * xi);
	}

	glm::dvec2 ToDegrees(const Projection& projection, glm::dvec2 eastNorth)
	{
		const Series& s = Krueger();
		const double xi = (eastNorth.y - projection.falseNorthing) / (projection.k0 * s.A);
		const double eta = (eastNorth.x - projection.falseEasting) / (projection.k0 * s.A);
		double xiPrime = xi, etaPrime = eta;
		for (int j = 1; j <= 6; j++) {
			xiPrime -= s.beta[j - 1] * std::sin(2.0 * j * xi) * std::cosh(2.0 * j * eta);
			etaPrime -= s.beta[j - 1] * std::cos(2.0 * j * xi) * std::sinh(2.0 * j * eta);
		}
		const double tauPrime = std::sin(xiPrime) / std::sqrt(std::sinh(etaPrime) * std::sinh(etaPrime) + std::cos(xiPrime) * std::cos(xiPrime));
		const double lambda = std::atan2(std::sinh(etaPrime), std::cos(xiPrime));
		//The latitude whose conformal latitude is tauPrime, by Newton's method (converges in a few steps)
		const double e2 = s.e * s.e;
		double tau = tauPrime;
		for (int i = 0; i < 6; i++) {
			const double tauI = ConformalTan(tau, s.e);
			const double step = (tauPrime - tauI) / std::sqrt(1.0 + tauI * tauI) * (1.0 + (1.0 - e2) * tau * tau)
				/ ((1.0 - e2) * std::sqrt(1.0 + tau * tau));
			tau += step;
			if (std::abs(step) < 1e-14) {
				break;
			}
		}
		return glm::dvec2(projection.lon0Degrees + lambda * degrees, std::atan(tau) * degrees);
	}

	glm::dvec2 PixelToSystem(const GeoReference& reference, glm::dvec2 pixel)
	{
		return reference.origin + pixel * reference.pixel;
	}

	glm::dvec2 SystemToPixel(const GeoReference& reference, glm::dvec2 point)
	{
		return (point - reference.origin) / reference.pixel;
	}

	std::optional<glm::dvec2> PixelToDegrees(const GeoReference& reference, glm::dvec2 pixel)
	{
		const glm::dvec2 point = PixelToSystem(reference, pixel);
		if (reference.epsg == degreesEpsg) {
			return point;
		}
		if (const std::optional<Projection> projection = TransverseMercatorOf(reference.epsg)) {
			return ToDegrees(*projection, point);
		}
		return std::nullopt;
	}

	std::optional<glm::dvec2> DegreesToPixel(const GeoReference& reference, glm::dvec2 lonLat)
	{
		if (reference.epsg == degreesEpsg) {
			return SystemToPixel(reference, lonLat);
		}
		if (const std::optional<Projection> projection = TransverseMercatorOf(reference.epsg)) {
			return SystemToPixel(reference, ToMetres(*projection, lonLat));
		}
		return std::nullopt;
	}

	GeoReference Cropped(const GeoReference& reference, int x, int y, int downsample)
	{
		GeoReference cropped = reference;
		cropped.origin = PixelToSystem(reference, glm::dvec2(x, y));
		cropped.pixel = reference.pixel * static_cast<double>(std::max(downsample, 1));
		return cropped;
	}

	bool RotatedTransformation(const std::vector<double>& transformation)
	{
		//Row-major 4x4: x = [0] i + [1] j + [3], y = [4] i + [5] j + [7]
		return transformation.size() >= 16 && (transformation[1] != 0.0 || transformation[4] != 0.0);
	}

	std::optional<GeoReference> FromGeoTiffTags(const std::vector<double>& scale, const std::vector<double>& tiepoint,
		const std::vector<double>& transformation, const std::vector<uint16_t>& keys)
	{
		GeoReference reference;
		if (transformation.size() >= 16) {
			//Row-major 4x4: x = [0] i + [1] j + [3], y = [4] i + [5] j + [7]
			if (RotatedTransformation(transformation) || transformation[0] == 0.0 || transformation[5] == 0.0) {
				return std::nullopt;
			}
			reference.origin = glm::dvec2(transformation[3], transformation[7]);
			reference.pixel = glm::dvec2(transformation[0], transformation[5]);
		}
		else if (scale.size() >= 2 && scale[0] > 0.0 && scale[1] > 0.0 && tiepoint.size() >= 6) {
			//Raster point (I, J) lies at (X, Y); rows go south
			reference.pixel = glm::dvec2(scale[0], -scale[1]);
			reference.origin = glm::dvec2(tiepoint[3] - tiepoint[0] * scale[0], tiepoint[4] + tiepoint[1] * scale[1]);
		}
		else {
			return std::nullopt;
		}
		//GeoKeyDirectory: 4 header shorts, then {key, location, count, value}; location 0 = the value is in place
		int model = 0, raster = 0, geographic = 0, projected = 0;
		for (size_t k = 4; k + 3 < keys.size(); k += 4) {
			if (keys[k + 1] != 0) {
				continue;
			}
			switch (keys[k]) {
			case 1024: model = keys[k + 3]; break;
			case 1025: raster = keys[k + 3]; break;
			case 2048: geographic = keys[k + 3]; break;
			case 3072: projected = keys[k + 3]; break;
			default: break;
			}
		}
		//32767 = user-defined: not a code the application knows
		if (model == 2 || (model == 0 && projected == 0 && geographic != 0)) {
			reference.epsg = degreesEpsg;
		}
		else if (projected != 0 && projected != 32767) {
			reference.epsg = projected;
		}
		//PixelIsPoint: the tie point is the centre of the pixel
		if (raster == 2) {
			reference.origin -= 0.5 * reference.pixel;
		}
		return reference;
	}
}
