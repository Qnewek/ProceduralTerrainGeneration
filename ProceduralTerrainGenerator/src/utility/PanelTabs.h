#pragma once

#include <functional>
#include <span>
#include <string>

//Tabs at the top of an ImGui window, in rows: a tab that does not fit goes to the next row. The content of the selected tab
//is below them, scrolled on its own. The tabs fill the rest of the window, so they are the last thing the window draws.
class PanelTabs
{
public:
	struct Tab {
		const char* label;
		std::function<void()> draw;
	};

	//Draws the tab rows and opens the content area; the caller draws the content of the returned tab, then calls End
	//@param id - unique within the window
	//@param labels - names of the tabs, not empty, unique; the selection is kept by name, so it survives a list that changes
	//	(e.g. with the mode), and falls back to the first tab when its name is gone
	//@return index of the selected tab
	int Begin(const char* id, std::span<const char* const> labels);
	//Closes the content area; call after every Begin
	void End();
	//Begin, the draw function of the selected tab, End
	void Draw(const char* id, std::span<const Tab> tabs);

	//Name of the selected tab as of the last Begin; empty before the first one
	const std::string& Selected() const { return selected; }
	//true if the last Begin changed the selected tab (also the first Begin)
	bool Changed() const { return changed; }

private:
	std::string selected;
	bool changed = false;
};
