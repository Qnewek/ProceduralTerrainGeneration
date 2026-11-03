#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

//Writes a file, creating missing folders. The data goes to a temporary file next to the target, which is renamed
//at the end, so a failed write keeps the previous file
//@param error - description of the problem on failure
//@return true on success
bool WriteFileAtomic(const std::filesystem::path& path, const void* data, size_t size, std::string& error);
