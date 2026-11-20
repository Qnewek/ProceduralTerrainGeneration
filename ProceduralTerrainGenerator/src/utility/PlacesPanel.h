#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Places.h"

//ImGui section with the named places of one mode (places::Place, one file). It keeps the list and its file; the subsystem
//says where the view is now and moves the view to a chosen place.
class PlacesPanel
{
public:
	//@param _file - file with the places, created by the first change
	explicit PlacesPanel(std::filesystem::path _file);

	//@param here - the place of the current view with the given name, called when the user adds a place
	//@return the place to go to, when the user asked for one in this frame
	std::optional<places::Place> ImGuiDraw(const std::function<places::Place(const std::string& name)>& here);

private:
	void Load();
	//Writes the list; on failure the status tells why and the list in memory stays
	void Save(const std::string& done);

	std::filesystem::path file;
	std::vector<places::Place> list;
	bool loaded = false;
	int selected = -1;
	char name[places::maxNameLength + 1] = "";
	std::string status;
	bool statusIsError = false;
};
