#include "EngineExport.h"

#include <system_error>

namespace engineexport {
	namespace {
		//Path relative to the pack with forward slashes, as the manifest gives them
		std::string PackPath(const std::filesystem::path& relative)
		{
			const std::u8string text = relative.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		//First regular file in folder whose name without extension is stem; empty if none or the folder does not exist
		std::filesystem::path FindTexture(const std::filesystem::path& folder, const char* stem)
		{
			std::error_code ec;
			for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code fileError;
				if (it->is_regular_file(fileError) && it->path().stem() == stem) {
					return it->path();
				}
			}
			return {};
		}

		bool CopyInto(const std::filesystem::path& from, const std::filesystem::path& packFolder, const std::filesystem::path& relative,
			std::string& error)
		{
			std::error_code ec;
			std::filesystem::create_directories((packFolder / relative).parent_path(), ec);
			if (!ec) {
				std::filesystem::copy_file(from, packFolder / relative, std::filesystem::copy_options::overwrite_existing, ec);
			}
			if (ec) {
				error = PackPath(relative) + ": " + ec.message();
				return false;
			}
			return true;
		}
	}

	bool CopyMaterialTextures(const std::vector<MaterialTextures>& sources, std::vector<exportpack::Material>& materials,
		const std::filesystem::path& packFolder, std::vector<exportpack::File>& files, std::string& error)
	{
		for (exportpack::Material& material : materials) {
			if (material.id < 0 || material.id >= static_cast<int>(sources.size())) {
				continue;
			}
			const MaterialTextures& source = sources[material.id];
			for (int c = 0; c < 3; c++) {
				material.fallbackColor[c] = source.fallbackColor[c];
			}
			material.tileMetres = source.tileMetres;
			if (material.coverage <= 0.0f) {
				continue;
			}
			struct Kind {
				const char* stem;
				std::string* path;
			};
			const Kind kinds[] = { { "albedo", &material.albedo }, { "normal", &material.normal }, { "roughness", &material.roughness } };
			for (const Kind& kind : kinds) {
				const std::filesystem::path from = FindTexture(source.folder, kind.stem);
				if (from.empty()) {
					continue;
				}
				const std::filesystem::path relative = std::filesystem::path("materials") / source.folder.filename() / from.filename();
				if (!CopyInto(from, packFolder, relative, error)) {
					return false;
				}
				*kind.path = PackPath(relative);
				std::string extension = PackPath(from.extension());
				if (!extension.empty() && extension[0] == '.') {
					extension.erase(0, 1);
				}
				files.push_back(exportpack::File{ *kind.path, std::string("material_") + kind.stem, extension, std::nullopt });
			}
		}
		return true;
	}

	bool CopyEngineScripts(const std::filesystem::path& source, const std::filesystem::path& packFolder, std::vector<exportpack::File>& files,
		std::string& error)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(source, ec)) {
			error = "Import scripts not found: " + PackPath(source);
			return false;
		}
		for (std::filesystem::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
			std::error_code fileError;
			if (!it->is_regular_file(fileError)) {
				continue;
			}
			const std::filesystem::path relative = std::filesystem::path("engine") / std::filesystem::relative(it->path(), source, fileError);
			if (fileError) {
				error = "Import scripts: " + fileError.message();
				return false;
			}
			if (!CopyInto(it->path(), packFolder, relative, error)) {
				return false;
			}
			files.push_back(exportpack::File{ PackPath(relative), "engine_script", "text", std::nullopt });
		}
		if (ec) {
			error = "Import scripts: " + ec.message();
			return false;
		}
		return true;
	}
}
