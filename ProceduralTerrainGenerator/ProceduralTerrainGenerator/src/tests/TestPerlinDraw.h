#pragma once

#include "Test.h"

namespace test
{
	class TestPerlinDraw : public Test
	{
	public:
		TestPerlinDraw();
		~TestPerlinDraw();

		void OnUpdate(float deltaTime) override;
		void OnRender() override;
		void OnImGuiRender() override;

	private:

	};
}