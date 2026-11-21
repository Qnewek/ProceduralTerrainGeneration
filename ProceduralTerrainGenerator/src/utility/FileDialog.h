#pragma once

#include <filesystem>
#include <optional>
#include <string>

//Native "open file" dialog (Windows), owned by the application window. Blocks the calling thread until it is closed.
//The working directory stays unchanged, so relative paths such as res/ keep working.
//@param filter - pairs of a description and patterns ending with an extra \0, e.g. L"Images\0*.png;*.jpg\0"
//@param startFolder - folder shown first; a missing one falls back to the system default
//@param error - description of the problem if the dialog failed, empty if the user cancelled
//@return the chosen file, nothing if cancelled or failed
std::optional<std::filesystem::path> OpenFileDialog(const wchar_t* filter, const std::filesystem::path& startFolder, std::string& error);

//Native "save file" dialog (Windows); asks before replacing an existing file. Blocks like OpenFileDialog.
//@param defaultExtension - added when the user types a name without one, without the dot (e.g. L"ptgmap")
//@param suggestedName - file name shown first, UTF-8; may be empty
//@return the chosen file, nothing if cancelled or failed
std::optional<std::filesystem::path> SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExtension, const std::filesystem::path& startFolder,
	const std::string& suggestedName, std::string& error);
