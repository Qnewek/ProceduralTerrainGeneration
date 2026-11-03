#include "FileDialog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <system_error>
#include <vector>

std::optional<std::filesystem::path> OpenFileDialog(const wchar_t* filter, const std::filesystem::path& startFolder, std::string& error)
{
	error.clear();

	std::error_code ec;
	std::filesystem::path start = std::filesystem::absolute(startFolder, ec);
	start.make_preferred();
	const std::wstring startText = start.wstring();

	//Large enough for the longest path Windows accepts
	std::vector<wchar_t> file(32768, L'\0');

	GLFWwindow* window = glfwGetCurrentContext();
	OPENFILENAMEW dialog{};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = window ? glfwGetWin32Window(window) : nullptr;
	dialog.lpstrFilter = filter;
	dialog.lpstrFile = file.data();
	dialog.nMaxFile = static_cast<DWORD>(file.size());
	dialog.lpstrInitialDir = ec ? nullptr : startText.c_str();
	dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (!GetOpenFileNameW(&dialog)) {
		//0 means the user closed the dialog without choosing a file
		const DWORD code = CommDlgExtendedError();
		if (code != 0) {
			error = "The file dialog failed (code " + std::to_string(code) + ")";
		}
		return std::nullopt;
	}
	return std::filesystem::path(file.data());
}
