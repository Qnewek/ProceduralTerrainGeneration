#include "FileIO.h"

#include <fstream>
#include <system_error>

bool WriteFileAtomic(const std::filesystem::path& path, const void* data, size_t size, std::string& error)
{
	std::error_code ec;
	if (path.has_parent_path()) {
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			error = "Cannot create the folder: " + ec.message();
			return false;
		}
	}

	std::filesystem::path temporary = path;
	temporary += ".tmp";
	bool written = false;
	{
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
		file.flush();
		written = file.good();
	}
	if (written) {
		std::filesystem::rename(temporary, path, ec);
		if (!ec) {
			return true;
		}
		error = "Cannot replace the file: " + ec.message();
	}
	else {
		error = "Cannot write the file";
	}
	std::filesystem::remove(temporary, ec);
	return false;
}
