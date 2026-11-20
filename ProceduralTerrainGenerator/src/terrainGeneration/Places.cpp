#include "Places.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#include "FileIO.h"
#include "Preset.h"

namespace places {
	namespace {
		using json = nlohmann::ordered_json;

		constexpr const char* placesKind = "places";
		constexpr int currentVersion = 1;

		float ReadFloat(const json& field, const std::string& path)
		{
			if (!field.is_number() || !std::isfinite(field.get<double>()) || std::abs(field.get<double>()) > 1e30) {
				throw std::runtime_error(path + " must be a number");
			}
			return static_cast<float>(field.get<double>());
		}

		const json& Field(const json& object, const char* key, const std::string& path)
		{
			auto it = object.find(key);
			if (it == object.end()) {
				throw std::runtime_error(path + "." + key + " is missing");
			}
			return *it;
		}

		Place ReadPlace(const json& entry, const std::string& path)
		{
			if (!entry.is_object()) {
				throw std::runtime_error(path + " must be an object");
			}
			Place place;
			const json& name = Field(entry, "name", path);
			if (!name.is_string() || name.get_ref<const std::string&>().empty() || name.get_ref<const std::string&>().size() > maxNameLength) {
				throw std::runtime_error(path + ".name must be a text of 1 to " + std::to_string(maxNameLength) + " bytes");
			}
			place.name = name.get<std::string>();

			const json& camera = Field(entry, "camera", path);
			if (!camera.is_array() || camera.size() != 3) {
				throw std::runtime_error(path + ".camera must be an array of 3 numbers");
			}
			for (int i = 0; i < 3; i++) {
				place.camera[i] = ReadFloat(camera[i], path + ".camera");
			}
			place.yaw = ReadFloat(Field(entry, "yaw", path), path + ".yaw");
			place.pitch = ReadFloat(Field(entry, "pitch", path), path + ".pitch");
			if (std::abs(place.pitch) > 90.0f) {
				throw std::runtime_error(path + ".pitch must be between -90 and 90 degrees");
			}

			const json& offset = Field(entry, "mapOffset", path);
			const auto validCoordinate = [](const json& element) {
				return element.is_number_integer() && std::abs(element.get<double>()) <= preset::maxMapOffset;
			};
			if (!offset.is_array() || offset.size() != 2 || !validCoordinate(offset[0]) || !validCoordinate(offset[1])) {
				throw std::runtime_error(path + ".mapOffset must be an array of 2 integers between -" + std::to_string(preset::maxMapOffset)
					+ " and " + std::to_string(preset::maxMapOffset));
			}
			place.mapOffset = glm::ivec2(static_cast<int>(offset[0].get<long long>()), static_cast<int>(offset[1].get<long long>()));
			return place;
		}
	}

	std::string ToJson(const std::vector<Place>& list)
	{
		json root;
		root["kind"] = placesKind;
		root["version"] = currentVersion;
		json entries = json::array();
		for (const Place& place : list) {
			json entry;
			entry["name"] = place.name;
			entry["camera"] = { place.camera.x, place.camera.y, place.camera.z };
			entry["yaw"] = place.yaw;
			entry["pitch"] = place.pitch;
			entry["mapOffset"] = { place.mapOffset.x, place.mapOffset.y };
			entries.push_back(std::move(entry));
		}
		root["places"] = std::move(entries);
		return root.dump(2);
	}

	bool FromJson(const std::string& text, std::vector<Place>& out, std::string& error)
	{
		try {
			const json root = json::parse(text);
			if (!root.is_object()) {
				throw std::runtime_error("places must be an object");
			}
			const json& kind = Field(root, "kind", "places");
			if (!kind.is_string() || kind.get_ref<const std::string&>() != placesKind) {
				throw std::runtime_error(std::string("Not a file of places, expected kind \"") + placesKind + "\"");
			}
			const json& version = Field(root, "version", "places");
			if (!version.is_number_integer() || version.get<long long>() != currentVersion) {
				throw std::runtime_error("Unsupported version of the places file");
			}
			const json& entries = Field(root, "places", "places");
			if (!entries.is_array() || entries.size() > maxPlaces) {
				throw std::runtime_error("places.places must be an array of at most " + std::to_string(maxPlaces) + " places");
			}
			std::vector<Place> list;
			for (size_t i = 0; i < entries.size(); i++) {
				list.push_back(ReadPlace(entries[i], "places.places[" + std::to_string(i) + "]"));
			}
			out = std::move(list);
			return true;
		}
		catch (const std::exception& e) {
			error = e.what();
			return false;
		}
	}

	bool Save(const std::filesystem::path& path, const std::vector<Place>& list, std::string& error)
	{
		if (list.size() > maxPlaces) {
			error = "At most " + std::to_string(maxPlaces) + " places can be saved";
			return false;
		}
		for (const Place& place : list) {
			if (place.name.empty() || place.name.size() > maxNameLength || !preset::ValidMapOffset(place.mapOffset)) {
				error = "A place has an empty or too long name, or a map offset out of range";
				return false;
			}
		}
		const std::string text = ToJson(list) + "\n";
		return WriteFileAtomic(path, text.data(), text.size(), error);
	}

	bool Load(const std::filesystem::path& path, std::vector<Place>& out, std::string& error)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			out.clear();
			return true;
		}
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			error = "Cannot open the file";
			return false;
		}
		std::ostringstream text;
		text << file.rdbuf();
		return FromJson(text.str(), out, error);
	}
}
