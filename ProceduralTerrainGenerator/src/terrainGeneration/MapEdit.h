#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

#include "BiomePaint.h"
#include "Grid.h"
#include "PlacedPlants.h"

//Operations on the working copy of the "Imported map" mode (heights in metres) and its undo history
namespace mapedit {
	//Box blur of the given radius (two separable passes, the edge texel repeated outside the map), blended with the map by strength
	//@param radius - texels; 0 or less = no change
	//@param strength - 0 = no change, 1 = the blurred map
	void Smooth(HeightField& map, int radius, float strength);

	//h * factor + offset
	void Rescale(HeightField& map, float factor, float offset);

	//Erosion is given heights in pixels / erosionTexelsPerUnit: with 1 m pixels these are TerrainGen units (height 1 = 256 m), which its
	//default parameters are tuned to, so the same parameters give a similar result on any map
	inline constexpr float erosionTexelsPerUnit = 256.0f;
	//@return factor turning metres into erosion units
	float ErosionScale(double pixelMetres);

	//State before an operation
	struct Step {
		std::string label;
		std::optional<HeightField> map;		//none = the operation did not change the map
		std::optional<float> seaLevel;		//metres; none = no sea
		std::optional<biomepaint::Layer> paint;	//none = the operation did not change the painted biomes
		std::optional<placedplants::Layer> plants;	//none = the operation did not change the plants placed by hand
	};

	//What Undo / Redo changed
	struct Restored {
		bool map = false;		//the step held a map
		bool seaLevel = false;	//the sea level is another one now
		bool paint = false;		//the step held painted biomes
		bool plants = false;	//the step held placed plants
	};

	//Undo and redo stacks of steps, together within a memory limit (the bytes of their maps)
	class History {
	public:
		explicit History(size_t limitBytes) : limit(limitBytes) {}

		//Drops the oldest undo steps, then the oldest redo steps, until the history fits
		void SetLimit(size_t bytes);
		size_t Limit() const { return limit; }
		size_t Bytes() const { return bytes; }

		//Adds the state before an operation and clears redo; the oldest steps are dropped to fit the limit
		//@return false if the step alone does not fit: the history is left empty
		bool Push(Step before);
		bool CanUndo() const { return !undo.empty(); }
		bool CanRedo() const { return !redo.empty(); }
		//Label of the step Undo / Redo would restore; empty if none
		const std::string& UndoLabel() const;
		const std::string& RedoLabel() const;
		//Swaps the current state with the step on top (the map, the painted biomes and the placed plants only when the step holds them,
		//the sea level always) and moves the step to the other stack
		//@return what changed; none if there is nothing to undo / redo
		std::optional<Restored> Undo(HeightField& map, std::optional<float>& seaLevel, biomepaint::Layer& paint, placedplants::Layer& plants);
		std::optional<Restored> Redo(HeightField& map, std::optional<float>& seaLevel, biomepaint::Layer& paint, placedplants::Layer& plants);
		void Clear();
		//Removes the step pushed last (the undo step on top), for an operation that failed after pushing it; nothing to undo = no-op
		void DropLast();

	private:
		static size_t BytesOf(const Step& step);
		std::optional<Restored> Move(std::deque<Step>& from, std::deque<Step>& to, HeightField& map, std::optional<float>& seaLevel,
			biomepaint::Layer& paint, placedplants::Layer& plants);
		void Fit();

		std::deque<Step> undo, redo;	//newest at the back
		size_t limit = 0;
		size_t bytes = 0;
	};
}
