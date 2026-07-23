#include "includes.hpp"

// Human-readable name for a virtual-key code, falling back to hex for keys Windows can't name (mouse buttons, etc).
static const char* KeyName(int vk)
{
	static char name[32];
	UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
	switch (vk) // extended keys need the extended-scancode bit to name correctly
	{
	case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
	case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
	case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
		sc |= 0x100;
		break;
	}
	if (sc && GetKeyNameTextA((LONG)(sc << 16), name, sizeof(name)) > 0)
		return name;
	snprintf(name, sizeof(name), "0x%02X", vk);
	return name;
}

void Menu::Init()
{
	ImGui::SetNextWindowSize({ 300, 480 }, ImGuiCond_Once);
	ImGui::Begin("phxgg ESP", nullptr, 0);

	const float footerH = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;

	ImGui::BeginChild("##content", ImVec2(0, -footerH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	if (ImGui::BeginTabBar("##tabs"))
	{
		if (ImGui::BeginTabItem("透视"))
		{
			ImGui::BeginChild("##esp_list", ImVec2(0, 0), false);

			ImGui::Checkbox("视野修改", &cfg->bFovChanger);
			if (cfg->bFovChanger)
				ImGui::SliderFloat("视野值", &cfg->fFovValue, 50.0f, 180.0f);

			ImGui::Checkbox("仅敌方", &cfg->bEnemyOnly);
			ImGui::Checkbox("角色可见性（感染模式）", &cfg->bForceCharacterVisibility);
			ImGui::Checkbox("方框", &cfg->bBox);
			ImGui::Checkbox("连线", &cfg->bLines);
			ImGui::Checkbox("姓名", &cfg->bNames);
			ImGui::Checkbox("角色", &cfg->bRoles);
			ImGui::Checkbox("骨骼", &cfg->bSkeleton);
			ImGui::Checkbox("距离", &cfg->bDistance);
			// ImGui::Checkbox("猎人弹药", &cfg->bHunterAmmo);
			ImGui::Checkbox("诱饵", &cfg->bDecoys);

			ImGui::Separator();
			ImGui::Text("颜色");

			if (ImGui::ColorButton("##colVisible", *(ImVec4*)cfg->colVisible))
				ImGui::OpenPopup("popup_colVisible");
			ImGui::SameLine();
			ImGui::Text("可见");
			if (ImGui::BeginPopup("popup_colVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colNotVisible", *(ImVec4*)cfg->colNotVisible))
				ImGui::OpenPopup("popup_colNotVisible");
			ImGui::SameLine();
			ImGui::Text("不可见");
			if (ImGui::BeginPopup("popup_colNotVisible"))
			{
				ImGui::ColorPicker4("##pick", cfg->colNotVisible);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colLines", *(ImVec4*)cfg->colLines))
				ImGui::OpenPopup("popup_colLines");
			ImGui::SameLine();
			ImGui::Text("连线");
			if (ImGui::BeginPopup("popup_colLines"))
			{
				ImGui::ColorPicker4("##pick", cfg->colLines);
				ImGui::EndPopup();
			}

			if (ImGui::ColorButton("##colDecoy", *(ImVec4*)cfg->colDecoy))
				ImGui::OpenPopup("popup_colDecoy");
			ImGui::SameLine();
			ImGui::Text("诱饵");
			if (ImGui::BeginPopup("popup_colDecoy"))
			{
				ImGui::ColorPicker4("##pick", cfg->colDecoy);
				ImGui::EndPopup();
			}

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("传送"))
		{
			ImGui::BeginChild("##tp_list", ImVec2(0, 0), false);

			if (cheat->PlayerInfos.empty())
			{
				ImGui::TextDisabled("未找到玩家");
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

		if (ImGui::BeginTabItem("工具"))
		{
			ImGui::BeginChild("##tools_list", ImVec2(0, 0), false);

			ImGui::Text("求生者");
			ImGui::Separator();
			ImGui::Checkbox("反检测", &cfg->bAntiDetection);
			ImGui::Checkbox("诱饵无冷却", &cfg->bNoDecoyCooldown);

			ImGui::Separator();
			ImGui::Text("猎人");
			ImGui::Separator();
			ImGui::Checkbox("枪械无冷却", &cfg->bNoGunCooldown);
			ImGui::Checkbox("无限子弹", &cfg->bInfiniteBullets);

			// Magnet toggle key rebind: click the button, then press any key (ESC cancels).
			static bool bindingMagnet = false;
			ImGui::Text("磁铁按键:");
			ImGui::SameLine();
			if (ImGui::Button(bindingMagnet ? "按下任意键..." : KeyName(cfg->iMagnetKey)))
				bindingMagnet = true;
			if (bindingMagnet)
			{
				for (int vk = 0x08; vk <= 0xFE; vk++)
				{
					if (vk == VK_LBUTTON || vk == VK_RBUTTON) // reserved for UI interaction
						continue;
					// 0x8000 = key is down, 0x0001 = key was pressed since last call
					if (GetAsyncKeyState(vk) & 0x8000)
					{
						if (vk != VK_ESCAPE)
							cfg->iMagnetKey = vk;
						bindingMagnet = false;
						break;
					}
				}
			}

			if (ImGui::Button("击杀所有求生者"))
				cheat->RequestKillAllSurvivors();

			ImGui::Separator();
			ImGui::Text("击杀指定玩家");

			// Track the pick by actor pointer, not list index - PlayerInfos is rebuilt every frame and
			// indices can drift. Resolve the selected actor's current name for the combo preview, and
			// drop the selection if that actor no longer exists this frame.
			static SDK::AActor* selectedKillActor = nullptr;
			const char* killPreview = "选择求生者";
			bool killStillPresent = false;
			int survivorCount = 0;
			for (const auto& p : cheat->PlayerInfos)
			{
				if (!p.IsSurvivor)
					continue; // only survivors can be killed
				survivorCount++;
				if (p.Actor == selectedKillActor)
				{
					killPreview = p.Name.c_str();
					killStillPresent = true;
				}
			}
			if (!killStillPresent)
				selectedKillActor = nullptr;
			if (survivorCount == 0)
				killPreview = "未找到求生者";

			// Combo on the left filling the row, fixed-width "Kill" button on the right.
			const float killBtnW = ImGui::CalcTextSize("击杀").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - killBtnW - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::BeginCombo("##kill_target", killPreview))
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					if (!cheat->PlayerInfos[i].IsSurvivor)
						continue;
					ImGui::PushID(i);
					const bool isSelected = (cheat->PlayerInfos[i].Actor == selectedKillActor);
					if (ImGui::Selectable(cheat->PlayerInfos[i].Name.c_str(), isSelected))
						selectedKillActor = cheat->PlayerInfos[i].Actor;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("击杀", ImVec2(killBtnW, 0)) && selectedKillActor)
				cheat->RequestKillSurvivor(selectedKillActor);

			ImGui::Separator();
			ImGui::Text("通用");
			ImGui::Separator();
			ImGui::Checkbox("反服务器踢出", &cfg->bPreventKick);

			ImGui::Separator();

			if (ImGui::Button("导出骨骼（调试）"))
				cfg->bDumpBones = true;

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("改名"))
		{
			ImGui::BeginChild("##name_list", ImVec2(0, 0), false);

			static SDK::AActor* selectedNameActor = nullptr;
			const char* namePreview = "选择玩家";
			std::string selectedName;
			bool nameStillPresent = false;
			for (const auto& p : cheat->PlayerInfos)
			{
				if (p.Actor == selectedNameActor)
				{
					namePreview = p.Name.c_str();
					selectedName = p.Name;
					nameStillPresent = true;
				}
			}
			if (!nameStillPresent)
				selectedNameActor = nullptr;
			if (cheat->PlayerInfos.empty())
				namePreview = "未找到玩家";

			const float nameBtnW = ImGui::CalcTextSize("更改").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - nameBtnW - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::BeginCombo("##name_target", namePreview))
			{
				for (int i = 0; i < (int)cheat->PlayerInfos.size(); i++)
				{
					ImGui::PushID(i);
					const bool isSelected = (cheat->PlayerInfos[i].Actor == selectedNameActor);
					if (ImGui::Selectable(cheat->PlayerInfos[i].Name.c_str(), isSelected))
						selectedNameActor = cheat->PlayerInfos[i].Actor;
					if (isSelected)
						ImGui::SetItemDefaultFocus();
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}

			ImGui::SameLine();
			if (ImGui::Button("更改", ImVec2(nameBtnW, 0)) && selectedNameActor && !selectedName.empty())
				cheat->RequestChangeName(selectedName);

			// Custom name: type anything and apply it to our own player.
			static char customName[64] = "";
			const float setBtnW = ImGui::CalcTextSize("设置").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - setBtnW - ImGui::GetStyle().ItemSpacing.x);
			const bool nameEntered = ImGui::InputText("##custom_name", customName, sizeof(customName), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("设置", ImVec2(setBtnW, 0)) || nameEntered) && customName[0] != '\0')
				cheat->RequestChangeName(customName);

			ImGui::EndChild();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}

	ImGui::EndChild();

	ImGui::Separator();

	float buttonW = 55.0f;
	if (ImGui::Button("保存", ImVec2(buttonW, 0)))
		cfg->SaveSettings();
	ImGui::SameLine();
	if (ImGui::Button("加载", ImVec2(buttonW, 0)))
		cfg->LoadSettings();

	ImGui::SameLine();
	float checkboxW = ImGui::CalcTextSize("启用").x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - checkboxW - ImGui::GetStyle().WindowPadding.x);
	ImGui::Checkbox("启用", &cfg->bInitHooks);

	ImGui::End();
}
