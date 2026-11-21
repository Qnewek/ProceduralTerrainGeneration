#include "FileDialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <system_error>
#include <vector>

namespace {
	//Shows the open or the save dialog; file holds the suggested name on entry and the chosen path on success
	std::optional<std::filesystem::path> RunDialog(bool save, const wchar_t* filter, const wchar_t* defaultExtension, const std::filesystem::path& startFolder,
		std::vector<wchar_t>& file, std::string& error)
	{
		error.clear();

		std::error_code ec;
		std::filesystem::path start = std::filesystem::absolute(startFolder, ec);
		start.make_preferred();
		const std::wstring startText = start.wstring();

		GLFWwindow* window = glfwGetCurrentContext();
		OPENFILENAMEW dialog{};
		dialog.lStructSize = sizeof(dialog);
		dialog.hwndOwner = window ? glfwGetWin32Window(window) : nullptr;
		dialog.lpstrFilter = filter;
		dialog.lpstrFile = file.data();
		dialog.nMaxFile = static_cast<DWORD>(file.size());
		dialog.lpstrInitialDir = ec ? nullptr : startText.c_str();
		dialog.lpstrDefExt = defaultExtension;
		dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);

		if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
			//0 means the user closed the dialog without choosing a file
			const DWORD code = CommDlgExtendedError();
			if (code != 0) {
				error = "The file dialog failed (code " + std::to_string(code) + ")";
			}
			return std::nullopt;
		}
		return std::filesystem::path(file.data());
	}

	//Large enough for the longest path Windows accepts
	constexpr size_t maxPath = 32768;
}

std::optional<std::filesystem::path> OpenFileDialog(const wchar_t* filter, const std::filesystem::path& startFolder, std::string& error)
{
	std::vector<wchar_t> file(maxPath, L'\0');
	return RunDialog(false, filter, nullptr, startFolder, file, error);
}

std::optional<std::filesystem::path> SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExtension, const std::filesystem::path& startFolder,
	const std::string& suggestedName, std::string& error)
{
	std::vector<wchar_t> file(maxPath, L'\0');
	const std::wstring name = std::filesystem::path(std::u8string(suggestedName.begin(), suggestedName.end())).wstring();
	std::copy_n(name.begin(), std::min(name.size(), maxPath - 1), file.begin());
	return RunDialog(true, filter, defaultExtension, startFolder, file, error);
}
