#pragma once

#include <filesystem>
#include <string>
#include <vector>

//ImGui section listing the preset files of one folder. It only asks for saving and loading; the subsystem
//owning the config does the work and reports the outcome back.
class PresetPanel
{
public:
	enum class Action {
		NONE,
		SAVE,
		LOAD
	};

	//@param _folder - folder with the presets, created by the first save
	explicit PresetPanel(std::filesystem::path _folder);

	//@return what the user asked for in this frame; use SavePath / LoadPath for the file
	Action ImGuiDraw();

	std::filesystem::path SavePath() const;
	const std::filesystem::path& LoadPath() const { return selected; }

	//Shows the outcome of an action below the controls
	void ReportSaved();
	void ReportLoaded();
	void ReportError(const std::string& message);

private:
	void Refresh();

	std::filesystem::path folder;
	std::vector<std::filesystem::path> files;
	std::filesystem::path selected;
	char name[65] = "";
	bool listed = false;
	std::string status;
	bool statusIsError = false;
};
