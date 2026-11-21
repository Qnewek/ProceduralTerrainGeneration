#include "MapEdit.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace mapedit {
	namespace {
		//Box blur along one line of count values, step apart in data, the edge value repeated outside
		void BlurLine(float* data, int count, size_t step, int radius, std::vector<float>& line)
		{
			line.resize(count);
			for (int i = 0; i < count; i++) {
				line[i] = data[i * step];
			}
			auto at = [&](int i) { return static_cast<double>(line[std::clamp(i, 0, count - 1)]); };
			double sum = 0.0;
			for (int i = -radius; i <= radius; i++) {
				sum += at(i);
			}
			const double scale = 1.0 / (2.0 * radius + 1.0);
			for (int i = 0; i < count; i++) {
				data[i * step] = static_cast<float>(sum * scale);
				sum += at(i + radius + 1) - at(i - radius);
			}
		}
	}

	void Smooth(HeightField& map, int radius, float strength)
	{
		if (radius <= 0 || strength <= 0.0f || map.Empty()) {
			return;
		}
		const int width = map.GetWidth(), height = map.GetHeight();
		HeightField blurred = map;
		std::vector<float> line;
		for (int y = 0; y < height; y++) {
			BlurLine(&blurred.At(0, y), width, 1, radius, line);
		}
		for (int x = 0; x < width; x++) {
			BlurLine(&blurred.At(x, 0), height, static_cast<size_t>(width), radius, line);
		}
		const float s = std::min(strength, 1.0f);
		for (size_t i = 0; i < map.Size(); i++) {
			map[i] += (blurred[i] - map[i]) * s;
		}
	}

	void Rescale(HeightField& map, float factor, float offset)
	{
		for (size_t i = 0; i < map.Size(); i++) {
			map[i] = map[i] * factor + offset;
		}
	}

	float ErosionScale(double pixelMetres)
	{
		return static_cast<float>(1.0 / (pixelMetres * erosionTexelsPerUnit));
	}

	size_t History::BytesOf(const Step& step)
	{
		return step.map ? step.map->Size() * sizeof(float) : 0;
	}

	void History::SetLimit(size_t _bytes)
	{
		limit = _bytes;
		Fit();
	}

	void History::Fit()
	{
		for (std::deque<Step>* stack : { &undo, &redo }) {
			while (bytes > limit && !stack->empty()) {
				bytes -= BytesOf(stack->front());
				stack->pop_front();
			}
		}
	}

	bool History::Push(Step before)
	{
		for (const Step& step : redo) {
			bytes -= BytesOf(step);
		}
		redo.clear();
		if (BytesOf(before) > limit) {
			Clear();
			return false;
		}
		bytes += BytesOf(before);
		undo.push_back(std::move(before));
		Fit();
		return true;
	}

	const std::string& History::UndoLabel() const
	{
		static const std::string none;
		return undo.empty() ? none : undo.back().label;
	}

	const std::string& History::RedoLabel() const
	{
		static const std::string none;
		return redo.empty() ? none : redo.back().label;
	}

	bool History::Move(std::deque<Step>& from, std::deque<Step>& to, HeightField& map, std::optional<float>& seaLevel)
	{
		if (from.empty()) {
			return false;
		}
		Step step = std::move(from.back());
		from.pop_back();
		if (step.map) {
			std::swap(*step.map, map);
		}
		std::swap(step.seaLevel, seaLevel);
		to.push_back(std::move(step));
		return true;
	}

	bool History::Undo(HeightField& map, std::optional<float>& seaLevel)
	{
		//The maps swapped have the same size, so the bytes do not change
		return Move(undo, redo, map, seaLevel);
	}

	bool History::Redo(HeightField& map, std::optional<float>& seaLevel)
	{
		return Move(redo, undo, map, seaLevel);
	}

	void History::Clear()
	{
		undo.clear();
		redo.clear();
		bytes = 0;
	}
}
