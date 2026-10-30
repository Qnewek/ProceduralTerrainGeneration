cmake_minimum_required(VERSION 3.25)
project(ProceduralTerrainGenerator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT, matches the x64-windows-static vcpkg triplet used by the presets
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)
find_package(Stb REQUIRED)

set(PTG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ProceduralTerrainGenerator)
set(PTG_SRC ${PTG_DIR}/src)

# Warnings as errors for our own code; dependency headers are included as external (no warnings)
function(ptg_set_warnings target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
	endif()
endfunction()

# Code kept in the repository (Simplex noise with per-instance permutations, tk::spline, stb_image implementation)
add_library(ptg_vendor STATIC
	${PTG_SRC}/vendor/Simplex/SimplexNoise.cpp
	${PTG_SRC}/vendor/stb_image/stb_image.cpp
)
target_include_directories(ptg_vendor SYSTEM PUBLIC ${PTG_SRC}/vendor ${Stb_INCLUDE_DIR})

# Terrain generation without OpenGL/ImGui - used by the app, the benchmark and tests
add_library(ptg_core STATIC
	${PTG_SRC}/terrainGeneration/Biome.cpp
	${PTG_SRC}/terrainGeneration/BiomeGenerator.cpp
	${PTG_SRC}/terrainGeneration/Erosion.cpp
	${PTG_SRC}/terrainGeneration/Noise.cpp
	${PTG_SRC}/terrainGeneration/TerrainGenerator.cpp
)
target_include_directories(ptg_core PUBLIC ${PTG_SRC}/terrainGeneration)
target_link_libraries(ptg_core PUBLIC ptg_vendor glm::glm)
ptg_set_warnings(ptg_core)

add_executable(ProceduralTerrainGenerator
	${PTG_SRC}/Application.cpp
	${PTG_SRC}/opengl/IndexBuffer.cpp
	${PTG_SRC}/opengl/Renderer.cpp
	${PTG_SRC}/opengl/Shader.cpp
	${PTG_SRC}/opengl/TerrainView.cpp
	${PTG_SRC}/opengl/TextureClass.cpp
	${PTG_SRC}/opengl/VertexArray.cpp
	${PTG_SRC}/opengl/VertexBuffer.cpp
	${PTG_SRC}/subSystems/NoiseBasedGenerationSys.cpp
	${PTG_SRC}/subSystems/TerrainGenApp.cpp
	${PTG_SRC}/subSystems/TerrainGenerationSys.cpp
	${PTG_SRC}/utility/Camera.cpp
	${PTG_SRC}/utility/LightSource.cpp
	${PTG_SRC}/utility/utilities.cpp
)
target_include_directories(ProceduralTerrainGenerator PRIVATE
	${PTG_SRC}
	${PTG_SRC}/opengl
	${PTG_SRC}/subSystems
	${PTG_SRC}/utility
)
target_link_libraries(ProceduralTerrainGenerator PRIVATE
	ptg_core
	OpenGL::GL
	GLEW::GLEW
	glfw
	imgui::imgui
	implot::implot
)
# Fallback working directory with res/, used when the exe is started from elsewhere (build dir, IDE)
target_compile_definitions(ProceduralTerrainGenerator PRIVATE PTG_RESOURCE_DIR="${PTG_DIR}")
ptg_set_warnings(ProceduralTerrainGenerator)
