#include "includes.hpp"

void Menu::Init()
{
	ImGui::SetNextWindowSize({ 300, 380 }, ImGuiCond_Once);
	ImGui::Begin("phxgg esp", nullptr, 0);

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("ESP"))
		{
			ImGui::Checkbox("Fov Changer", &cfg->bFovChanger);
			if (cfg->bFovChanger)
				ImGui::SliderFloat("Fov Value", &cfg->fFovValue, 50.0f, 180.0f);

			ImGui::Checkbox("Enemy Only", &cfg->bEnemyOnly);
			ImGui::Checkbox("Box", &cfg->bBox);
			ImGui::Checkbox("Lines", &cfg->bLines);
			ImGui::Checkbox("Name", &cfg->bNames);
			ImGui::Checkbox("Skeleton", &cfg->bSkeleton);

			ImGui::Separator();
			ImGui::Text("Colors");

			if (ImGui::ColorButton("##colVisible", *(ImVec4*)cfg->colVisible))
				ImGui::OpenPopup("popup_colVisible");
			ImGui::SameLine(); ImGui::Text("Visible");
			if (ImGui::BeginPopup("popup_colVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colNotVisible", *(ImVec4*)cfg->colNotVisible))
				ImGui::OpenPopup("popup_colNotVisible");
			ImGui::SameLine(); ImGui::Text("Not Visible");
			if (ImGui::BeginPopup("popup_colNotVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colNotVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colLines", *(ImVec4*)cfg->colLines))
				ImGui::OpenPopup("popup_colLines");
			ImGui::SameLine(); ImGui::Text("Lines");
			if (ImGui::BeginPopup("popup_colLines"))
			{
				ImGui::ColorPicker4("##pick", cfg->colLines);
				ImGui::EndPopup();
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Teleport"))
		{
			if (cheat->PlayerInfos.empty())
			{
				ImGui::TextDisabled("No players found");
			}
			else
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					ImGui::PushID(i);
					if (ImGui::Button("TP"))
						cfg->iTeleportTarget = i;
					ImGui::SameLine();
					ImGui::Text("%s", cheat->PlayerInfos[i].Name.c_str());
					ImGui::PopID();
				}
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tools"))
		{
			if (ImGui::Button("Dump Bones"))
				cfg->bDumpBones = true;
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("About"))
		{
			ImGui::Text("phxgg esp");
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	// Footer: Save / Load on left, Enable on the right
	const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
	ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footerHeight);
	ImGui::Separator();

	float buttonW = 55.0f;
	if (ImGui::Button("Save", ImVec2(buttonW, 0)))
		cfg->SaveSettings();
	ImGui::SameLine();
	if (ImGui::Button("Load", ImVec2(buttonW, 0)))
		cfg->LoadSettings();

	ImGui::SameLine();
	float checkboxW = ImGui::CalcTextSize("Enable").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - checkboxW - ImGui::GetStyle().WindowPadding.x);
	ImGui::Checkbox("Enable", &cfg->bInitHooks);

	ImGui::End();
}
