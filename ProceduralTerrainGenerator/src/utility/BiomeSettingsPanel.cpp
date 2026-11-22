#include "BiomeSettingsPanel.h"

#include "imgui.h"

namespace biomepanel {
	bool Blend(BiomeConfig& config)
	{
		const bool changed = ImGui::SliderFloat("Blend width", &config.blendWidth, 0.0f, 0.5f);
		ImGui::SetItemTooltip("How far (in noise values) a biome fades into the nearest one; 0 = sharp borders");
		return changed;
	}

	bool Climate(biome::Climate& climate)
	{
		ImGui::SeparatorText("Climate");
		bool changed = ImGui::SliderFloat("Lapse rate", &climate.lapseRate, 0.0f, 3.0f);
		ImGui::SetItemTooltip("Temperature drop per unit of terrain height above the water (heights are in [0, 1]).\n"
			"Makes high mountains cold: snowy peaks without a rule of their own");
		changed |= ImGui::SliderFloat("Coast humidity", &climate.coastHumidity, -1.0f, 1.0f);
		ImGui::SetItemTooltip("Humidity added at the water surface, fading out with height");
		//Divides the height in the shader, so it stays above 0
		changed |= ImGui::SliderFloat("Coast range", &climate.coastRange, 0.005f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Height above the water where the coast humidity fades to 0");
		return changed;
	}

	bool Materials(BiomeConfig& config, bool showSeed)
	{
		ImGui::SeparatorText("Surface materials");
		ImGui::TextDisabled("Seen in the TEXTURED display mode; each biome's mix is in the biome table");
		//Both divide in the shader, so they stay above 0
		bool changed = ImGui::SliderFloat("Material blend width", &config.materialBlendWidth, 0.01f, 0.5f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("How far (in noise values) the materials of neighbouring biomes mix");
		changed |= ImGui::SliderFloat("Patchiness", &config.materialPatchiness, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("How strongly noise breaks a biome's mix into patches of its materials; 0 = an even mix.\n"
			"At 1 each material covers about its share of the biome's area");
		changed |= ImGui::SliderFloat("Patch contrast", &config.materialPatchContrast, 1.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		ImGui::SetItemTooltip("How sharp the edges of the patches are; 1 = soft gradients. Takes full effect at patchiness 1");
		changed |= ImGui::SliderFloat("Patch size", &config.materialPatchSize, 2.0f, 256.0f, "%.0f texels", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
		if (showSeed) {
			ImGui::SetItemTooltip("Wavelength of the patch noise; the detail seed chooses its pattern");
			changed |= ImGui::InputInt("Detail seed", &config.detailSeed);
			ImGui::SetItemTooltip("Chooses the pattern of the material patches");
		}
		else {
			ImGui::SetItemTooltip("Wavelength of the patch noise in map texels; the detail seed above chooses its pattern");
		}
		return changed;
	}
}
