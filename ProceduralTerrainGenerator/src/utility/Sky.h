#pragma once

#include <memory>

#include "Camera.h"
#include "LightSource.h"
#include "Shader.h"
#include "VertexArray.h"

//Background of the 3D view: sky gradient that follows the sun elevation, with the sun disc.
//Drawn first, over the whole viewport and without depth; the 2D map view draws no sky
class Sky
{
public:
	bool Initialize();
	void ReloadShader() { shader->Reload(); }
	void Draw(Camera& camera, const LightSource& sun);

private:
	std::unique_ptr<Shader> shader;
	//Core profile needs a bound vertex array even without vertex data
	std::unique_ptr<VertexArray> vao;
};
