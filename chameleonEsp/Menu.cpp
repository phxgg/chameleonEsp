#include "includes.hpp"

void Menu::Init()
{
	ImGui::SetNextWindowSize({ 300, 420 }, ImGuiCond_Once);
	ImGui::Begin("phxgg esp", nullptr, 0);

	const float footerH = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;

	ImGui::BeginChild("##content", ImVec2(0, -footerH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("ESP"))
		{
			ImGui::BeginChild("##esp_list", ImVec2(0, 0), false);

			ImGui::Checkbox("Fov Changer", &cfg->bFovChanger);
			if (cfg->bFovChanger)
				ImGui::SliderFloat("Fov Value", &cfg->fFovValue, 50.0f, 180.0f);

			ImGui::Checkbox("Enemy Only", &cfg->bEnemyOnly);
			ImGui::Checkbox("Character Visibility (Infection Mode)", &cfg->bForceCharacterVisibility);
			ImGui::Checkbox("Box", &cfg->bBox);
			ImGui::Checkbox("Lines", &cfg->bLines);
			ImGui::Checkbox("Name", &cfg->bNames);
			ImGui::Checkbox("Roles", &cfg->bRoles);
			ImGui::Checkbox("Skeleton", &cfg->bSkeleton);
			ImGui::Checkbox("Distance", &cfg->bDistance);

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

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Teleport"))
		{
			ImGui::BeginChild("##tp_list", ImVec2(0, 0), false);

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
						cheat->RequestTeleport(cheat->PlayerInfos[i].Actor);
					ImGui::SameLine();
					ImGui::Text("%s", cheat->PlayerInfos[i].Name.c_str());
					ImGui::PopID();
				}
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tools"))
		{
			ImGui::BeginChild("##tools_list", ImVec2(0, 0), false);

			ImGui::Checkbox("Anti Detection (Survivors)", &cfg->bAntiDetection);
			ImGui::Checkbox("No Gun Cooldown (Hunters)", &cfg->bNoGunCooldown);
			ImGui::Checkbox("Anti Server Kick", &cfg->bPreventKick);

			ImGui::Separator();
			ImGui::TextUnformatted("Likes");

			static SDK::AActor* selectedPlayer = nullptr;
			static int likesAmount = 1;
			const float okButtonWidth = 42.0f;
			const float likesInputWidth = 55.0f;
			const float rowSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
			const float comboWidth = ImGui::GetContentRegionAvail().x - likesInputWidth - okButtonWidth - rowSpacing * 2.0f;
			const float fixedComboWidth = comboWidth > 0.0f ? comboWidth : 0.0f;

			if (!cheat->PlayerInfos.empty())
			{
				bool selectedPlayerStillExists = false;
				for (const auto& playerInfo : cheat->PlayerInfos)
				{
					if (playerInfo.Actor == selectedPlayer)
					{
						selectedPlayerStillExists = true;
						break;
					}
				}

				if (!selectedPlayerStillExists)
					selectedPlayer = cheat->PlayerInfos.front().Actor;

				const char* previewText = "Select player";
				for (const auto& playerInfo : cheat->PlayerInfos)
				{
					if (playerInfo.Actor == selectedPlayer)
					{
						previewText = playerInfo.Name.c_str();
						break;
					}
				}

				ImGui::SetNextItemWidth(fixedComboWidth);
				if (ImGui::BeginCombo("##likes_player", previewText))
				{
					for (const auto& playerInfo : cheat->PlayerInfos)
					{
						const bool isSelected = playerInfo.Actor == selectedPlayer;
						if (ImGui::Selectable(playerInfo.Name.c_str(), isSelected))
							selectedPlayer = playerInfo.Actor;

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}

					ImGui::EndCombo();
				}
			}
			else
			{
				ImGui::SetNextItemWidth(fixedComboWidth);
				ImGui::BeginDisabled();
				ImGui::BeginCombo("##likes_player", "No players found");
				ImGui::EndCombo();
				ImGui::EndDisabled();
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(likesInputWidth);
			ImGui::InputInt("##likes_amount", &likesAmount);
			if (likesAmount < 1)
				likesAmount = 1;

			ImGui::SameLine();
			const bool canApplyLikes = selectedPlayer != nullptr && !cheat->PlayerInfos.empty();
			if (!canApplyLikes)
				ImGui::BeginDisabled();
			ImGui::SetNextItemWidth(okButtonWidth);
			if (ImGui::Button("OK"))
				cheat->ApplyLikesToPlayer(selectedPlayer, likesAmount);
			if (!canApplyLikes)
				ImGui::EndDisabled();

			if (ImGui::Button("Dump Bones"))
				cfg->bDumpBones = true;

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("About"))
		{
			ImGui::BeginChild("##about_list", ImVec2(0, 0), false);

			ImGui::Text("phxgg esp");

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::EndChild();

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
