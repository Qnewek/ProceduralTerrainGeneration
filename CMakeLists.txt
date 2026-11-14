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
find_package(nlohmann_json CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
# cgltf is a single header (the implementation is compiled in VegetationMesh.cpp)
find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)

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

# Code kept in the repository (Simplex noise with per-instance permutations, stb_image implementation)
add_library(ptg_vendor STATIC
	${PTG_SRC}/vendor/Simplex/SimplexNoise.cpp
	${PTG_SRC}/vendor/stb_image/stb_image.cpp
)
target_include_directories(ptg_vendor SYSTEM PUBLIC ${PTG_SRC}/vendor ${Stb_INCLUDE_DIR})

# World parameters, presets, erosion and map files, without OpenGL/ImGui - used by the app and tests.
# Generation itself runs only in compute shaders (res/shaders/ComputeShaders)
add_library(ptg_core STATIC
	${PTG_SRC}/terrainGeneration/Biome.cpp
	${PTG_SRC}/terrainGeneration/Erosion.cpp
	${PTG_SRC}/terrainGeneration/FileIO.cpp
	${PTG_SRC}/terrainGeneration/HeightmapExport.cpp
	${PTG_SRC}/terrainGeneration/HeightmapImport.cpp
	${PTG_SRC}/terrainGeneration/LandStats.cpp
	${PTG_SRC}/terrainGeneration/Preset.cpp
	${PTG_SRC}/terrainGeneration/SplineSampling.cpp
	${PTG_SRC}/terrainGeneration/VegetationMesh.cpp
	${PTG_SRC}/terrainGeneration/VegetationPlacement.cpp
)
target_include_directories(ptg_core PUBLIC ${PTG_SRC}/terrainGeneration)
target_include_directories(ptg_core SYSTEM PRIVATE ${CGLTF_INCLUDE_DIRS})
target_link_libraries(ptg_core PUBLIC ptg_vendor glm::glm)
target_link_libraries(ptg_core PRIVATE nlohmann_json::nlohmann_json lodepng meshoptimizer::meshoptimizer)
ptg_set_warnings(ptg_core)

add_executable(ProceduralTerrainGenerator
	${PTG_SRC}/Application.cpp
	${PTG_SRC}/opengl/ChunkWorld.cpp
	${PTG_SRC}/opengl/GpuHeightCheck.cpp
	${PTG_SRC}/opengl/GpuHeightGenerator.cpp
	${PTG_SRC}/opengl/GpuQuery.cpp
	${PTG_SRC}/opengl/GpuSurfacePass.cpp
	${PTG_SRC}/opengl/IndexBuffer.cpp
	${PTG_SRC}/opengl/NoisePreview.cpp
	${PTG_SRC}/opengl/Renderer.cpp
	${PTG_SRC}/opengl/Shader.cpp
	${PTG_SRC}/opengl/TerrainMaterials.cpp
	${PTG_SRC}/opengl/TerrainView.cpp
	${PTG_SRC}/opengl/TextureClass.cpp
	${PTG_SRC}/opengl/VegetationBench.cpp
	${PTG_SRC}/opengl/VegetationCheck.cpp
	${PTG_SRC}/opengl/VegetationModels.cpp
	${PTG_SRC}/opengl/VegetationLayer.cpp
	${PTG_SRC}/opengl/VertexArray.cpp
	${PTG_SRC}/opengl/VertexBuffer.cpp
	${PTG_SRC}/subSystems/MapImportSys.cpp
	${PTG_SRC}/subSystems/NoiseBasedGenerationSys.cpp
	${PTG_SRC}/subSystems/TerrainGenApp.cpp
	${PTG_SRC}/subSystems/TerrainGenerationSys.cpp
	${PTG_SRC}/utility/Camera.cpp
	${PTG_SRC}/utility/ExportPanel.cpp
	${PTG_SRC}/utility/BiomeEditor.cpp
	${PTG_SRC}/utility/FileDialog.cpp
	${PTG_SRC}/utility/FrameMetrics.cpp
	${PTG_SRC}/utility/LightSource.cpp
	${PTG_SRC}/utility/PresetPanel.cpp
	${PTG_SRC}/utility/Sky.cpp
	${PTG_SRC}/utility/utilities.cpp
	${PTG_SRC}/utility/PanelTabs.cpp
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
	nlohmann_json::nlohmann_json
	lodepng
	comdlg32
)
# Fallback working directory with res/, used when the exe is started from elsewhere (build dir, IDE)
target_compile_definitions(ProceduralTerrainGenerator PRIVATE PTG_RESOURCE_DIR="${PTG_DIR}")
ptg_set_warnings(ProceduralTerrainGenerator)

# Unit tests of ptg_core, run with: ctest --preset x64-release
enable_testing()
add_executable(ptg_tests
	${PTG_DIR}/tests/BiomeTests.cpp
	${PTG_DIR}/tests/ErosionTests.cpp
	${PTG_DIR}/tests/FlightRecordTests.cpp
	${PTG_DIR}/tests/FrustumTests.cpp
	${PTG_DIR}/tests/GridTests.cpp
	${PTG_DIR}/tests/HeightmapExportTests.cpp
	${PTG_DIR}/tests/HeightmapImportTests.cpp
	${PTG_DIR}/tests/ImpostorTests.cpp
	${PTG_DIR}/tests/LandStatsTests.cpp
	${PTG_DIR}/tests/PermutationTests.cpp
	${PTG_DIR}/tests/PresetTests.cpp
	${PTG_DIR}/tests/RangeAllocatorTests.cpp
	${PTG_DIR}/tests/SplineTests.cpp
	${PTG_DIR}/tests/RollingStatsTests.cpp
	${PTG_DIR}/tests/VegetationMeshTests.cpp
	${PTG_DIR}/tests/VegetationPlacementTests.cpp
	${PTG_DIR}/tests/TestMain.cpp
)
target_include_directories(ptg_tests PRIVATE ${PTG_SRC}/utility)
target_link_libraries(ptg_tests PRIVATE ptg_core doctest::doctest lodepng)
ptg_set_warnings(ptg_tests)
add_test(NAME ptg_tests COMMAND ptg_tests)
