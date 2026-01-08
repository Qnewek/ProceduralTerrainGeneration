#pragma once

class HeightGenerator
{
	public:
	virtual ~HeightGenerator() = default;
	virtual bool Initialize() = 0;
	virtual float GetVal(float x, float y) = 0;
};