#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "nexus/Nexus.h"
#include "mumble/Mumble.h"
#include "imgui/imgui.h"

#include "Remote.h"
#include "Version.h"

#include "resource.h"

#include "nlohmann/json.hpp"
using json = nlohmann::json;

namespace ImGui
{
	static bool Tooltip()
	{
		bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			ImGui::BeginTooltip();
		}
		return hovered;
	}

	static void TooltipGeneric(const char* fmt, ...)
	{
		if (ImGui::Tooltip())
		{
			ImGui::Text(fmt);
			ImGui::EndTooltip();
		}
	}
}

std::map<unsigned short, std::string> ScancodeLookupTable;

const char* ConvertToUTF8(const char* multibyteStr)
{
	char* utf8Str = nullptr;

	int wideCharCount = MultiByteToWideChar(CP_ACP, 0, multibyteStr, -1, NULL, 0);
	if (wideCharCount > 0)
	{
		wchar_t* wideCharBuff = new wchar_t[wideCharCount];
		MultiByteToWideChar(CP_ACP, 0, multibyteStr, -1, wideCharBuff, wideCharCount);

		int utf8Count = WideCharToMultiByte(CP_UTF8, 0, wideCharBuff, -1, NULL, 0, NULL, NULL);
		if (utf8Count > 0)
		{
			utf8Str = new char[utf8Count];
			WideCharToMultiByte(CP_UTF8, 0, wideCharBuff, -1, utf8Str, utf8Count, NULL, NULL);
		}

		delete[] wideCharBuff;
	}

	return utf8Str;
}

bool operator==(const Keybind& lhs, const Keybind& rhs)
{
	return	lhs.Key == rhs.Key &&
		lhs.Alt == rhs.Alt &&
		lhs.Ctrl == rhs.Ctrl &&
		lhs.Shift == rhs.Shift;
}

bool operator!=(const Keybind& lhs, const Keybind& rhs)
{
	return	!(lhs == rhs);
}

std::string KeybindToString(Keybind& keybind, bool padded)
{
	if (keybind == Keybind{}) { return "(null)"; }

	char* buff = new char[100];
	std::string str;

	if (keybind.Alt)
	{
		GetKeyNameTextA(MapVirtualKeyA(VK_MENU, MAPVK_VK_TO_VSC) << 16, buff, 100);
		str.append(buff);
		str.append(padded ? " + " : "+");
	}

	if (keybind.Ctrl)
	{
		GetKeyNameTextA(MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC) << 16, buff, 100);
		str.append(buff);
		str.append(padded ? " + " : "+");
	}

	if (keybind.Shift)
	{
		GetKeyNameTextA(MapVirtualKeyA(VK_SHIFT, MAPVK_VK_TO_VSC) << 16, buff, 100);
		str.append(buff);
		str.append(padded ? " + " : "+");
	}

	HKL hkl = GetKeyboardLayout(0);
	UINT vk = MapVirtualKeyA(keybind.Key, MAPVK_VSC_TO_VK);

	if (vk >= 65 && vk <= 90 || vk >= 48 && vk <= 57)
	{
		GetKeyNameTextA(keybind.Key << 16, buff, 100);
		str.append(buff);
	}
	else
	{
		auto it = ScancodeLookupTable.find(keybind.Key);
		if (it != ScancodeLookupTable.end())
		{
			str.append(it->second);
		}
	}

	delete[] buff;

	std::transform(str.begin(), str.end(), str.begin(), ::toupper);

	// Convert Multibyte encoding to UFT-8 bytes
	const char* multibyte_pointer = str.c_str();
	const char* utf8_bytes = ConvertToUTF8(multibyte_pointer);

	return std::string(utf8_bytes);
}

typedef struct KeystrokeMessageFlags
{
	unsigned RepeatCount : 16;
	unsigned ScanCode : 8;
	unsigned ExtendedFlag : 1;
	unsigned Reserved : 4;
	unsigned ContextCode : 1;
	unsigned PreviousKeyState : 1;
	unsigned TransitionState : 1;

	unsigned short GetScanCode()
	{
		unsigned short ret = ScanCode;

		if (ExtendedFlag)
		{
			ret |= 0xE000;
		}

		return ret;
	}
} KeyLParam;

KeystrokeMessageFlags& LParamToKMF(LPARAM& lp)
{
	return *(KeystrokeMessageFlags*)&lp;
}

LPARAM& KMFToLParam(KeystrokeMessageFlags& kmf)
{
	return *(LPARAM*)&kmf;
}

LPARAM GetLPARAM(uint32_t key, bool down, bool sys)
{
	uint64_t lp;
	lp = down ? 0 : 1; // transition state
	lp = lp << 1;
	lp += down ? 0 : 1; // previous key state
	lp = lp << 1;
	lp += 0; // context code
	lp = lp << 1;
	lp = lp << 4;
	lp = lp << 1;
	lp = lp << 8;
	lp += MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
	lp = lp << 16;
	lp += 1;

	//APIDefs->Log(ELogLevel_TRACE, std::to_string(lp).c_str());

	return lp;
}

void AddonLoad(AddonAPI* aApi);
void AddonUnload();
void ProcessKeybind(const char* aIdentifier);
UINT AddonWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AddonRender();
void AddonOptions();
void PerformSudoku();

void LoadSettings(std::filesystem::path aPath);
void SaveSettings(std::filesystem::path aPath);

HWND Game;
HMODULE hSelf;
AddonDefinition AddonDef{};
AddonAPI* APIDefs = nullptr;
NexusLinkData* NexusLink = nullptr;
Mumble::Data* MumbleLink = nullptr;

std::filesystem::path AddonPath{};
json Settings{};
std::filesystem::path SettingsPath{};
std::mutex Mutex;

bool IsSlashGGButtonVisible = true;
bool IsSlashGGButtonHovered = false;
Texture* Button = nullptr;
Texture* ButtonHover = nullptr;

std::mutex GGMutex;
std::thread GGThread;
bool IsGGThreadRunning = false;
bool DoGG = false;

Keybind OpenChatKeybind{};

bool isEditingPosition = false;

Keybind CurrentKeybind{};
bool isSettingKeybind = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH: hSelf = hModule; break;
	case DLL_PROCESS_DETACH: break;
	case DLL_THREAD_ATTACH: break;
	case DLL_THREAD_DETACH: break;
	}
	return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef()
{
	AddonDef.Signature = 0x36100171;
	AddonDef.APIVersion = NEXUS_API_VERSION;
	AddonDef.Name = "/gg";
	AddonDef.Version.Major = V_MAJOR;
	AddonDef.Version.Minor = V_MINOR;
	AddonDef.Version.Build = V_BUILD;
	AddonDef.Version.Revision = V_REVISION;
	AddonDef.Author = "Raidcore";
	AddonDef.Description = "Lets you GG with a button press.";
	AddonDef.Load = AddonLoad;
	AddonDef.Unload = AddonUnload;
	AddonDef.Flags = EAddonFlags_None;

	/* not necessary if hosted on Raidcore, but shown anyway for the example also useful as a backup resource */
	AddonDef.Provider = EUpdateProvider_GitHub;
	AddonDef.UpdateLink = REMOTE_URL;

	return &AddonDef;
}

void AddonLoad(AddonAPI* aApi)
{
	APIDefs = aApi;
	ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
	ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc, (void(*)(void*, void*))APIDefs->ImguiFree); // on imgui 1.80+

	NexusLink = (NexusLinkData*)APIDefs->GetResource("DL_NEXUS_LINK");
	MumbleLink = (Mumble::Data*)APIDefs->GetResource("DL_MUMBLE_LINK");

	APIDefs->RegisterRender(ERenderType_Render, AddonRender);
	APIDefs->RegisterRender(ERenderType_OptionsRender, AddonOptions);

	APIDefs->RegisterWndProc(AddonWndProc); // lazy way to get game handle

	for (long long i = 0; i < 255; i++)
	{
		KeyLParam key{};
		key.ScanCode = i;
		char* buff = new char[64];
		std::string str;
		GetKeyNameTextA(static_cast<LONG>(KMFToLParam(key)), buff, 64);
		str.append(buff);

		ScancodeLookupTable[key.GetScanCode()] = str;

		key.ExtendedFlag = 1;
		buff = new char[64];
		str = "";
		GetKeyNameTextA(static_cast<LONG>(KMFToLParam(key)), buff, 64);
		str.append(buff);

		ScancodeLookupTable[key.GetScanCode()] = str;

		delete[] buff;
	}

	APIDefs->RegisterKeybindWithString("KB_SUDOKU", ProcessKeybind, "CTRL+K");

	AddonPath = APIDefs->GetAddonDirectory("SlashGG");
	SettingsPath = APIDefs->GetAddonDirectory("SlashGG/settings.json");
	std::filesystem::create_directory(AddonPath);
	LoadSettings(SettingsPath);

	IsGGThreadRunning = true;
	GGThread = std::thread(PerformSudoku);
	GGThread.detach();
}
void AddonUnload()
{
	APIDefs->DeregisterWndProc(AddonWndProc);

	APIDefs->DeregisterRender(AddonOptions);
	APIDefs->DeregisterRender(AddonRender);

	MumbleLink = nullptr;
	NexusLink = nullptr;

	// this ends the thread, I'm too lazy to implement proper logic right now
	IsGGThreadRunning = false;
	DoGG = true;
}

void ProcessKeybind(const char* aIdentifier)
{
	if (strcmp(aIdentifier, "KB_SUDOKU") == 0)
	{
		DoGG = true;
		return;
	}
}

UINT AddonWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	Game = hWnd;

	if (WM_KEYDOWN == uMsg || WM_SYSKEYDOWN == uMsg)
	{
		KeyLParam keylp = LParamToKMF(lParam);

		Keybind kb{};
		kb.Alt = GetKeyState(VK_MENU) & 0x8000;
		kb.Ctrl = GetKeyState(VK_CONTROL) & 0x8000;
		kb.Shift = GetKeyState(VK_SHIFT) & 0x8000;
		kb.Key = keylp.GetScanCode();

		// if shift, ctrl or alt set key to 0
		if (wParam == 16 || wParam == 17 || wParam == 18)
		{
			kb.Key = 0;
			if (wParam == 16) { kb.Shift = true; }
			if (wParam == 17) { kb.Ctrl = true; }
			if (wParam == 18) { kb.Alt = true; }
		}

		if (isSettingKeybind)
		{
			CurrentKeybind = kb;
		}
	}
	else if (WM_KEYUP == uMsg || WM_SYSKEYUP == uMsg)
	{
		KeyLParam keylp = LParamToKMF(lParam);

		Keybind kb{};
		kb.Alt = GetKeyState(VK_MENU) & 0x8000;
		kb.Ctrl = GetKeyState(VK_CONTROL) & 0x8000;
		kb.Shift = GetKeyState(VK_SHIFT) & 0x8000;
		kb.Key = keylp.GetScanCode();

		// if shift, ctrl or alt set key to 0
		if (wParam == 16 || wParam == 17 || wParam == 18)
		{
			kb.Key = 0;
			if (wParam == 16) { kb.Shift = true; }
			if (wParam == 17) { kb.Ctrl = true; }
			if (wParam == 18) { kb.Alt = true; }
		}
	}

	if (isSettingKeybind && (uMsg == WM_SYSKEYDOWN || uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_KEYUP))
	{
		return 0;
	}

	return uMsg;
}

void AddonRender()
{
	if (!NexusLink || !NexusLink->IsGameplay || !MumbleLink || MumbleLink->Context.IsMapOpen || !IsSlashGGButtonVisible || (MumbleLink->Context.MapType != Mumble::EMapType::Instance && !isEditingPosition))
	{
		return;
	}

	if (isEditingPosition)
	{
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.0f, 0.0f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
	}

	if (ImGui::Begin("Sudoku!", (bool*)0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | (isEditingPosition ? 0 : ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground)))
	{
		if (!Button || !ButtonHover)
		{
			Button = APIDefs->GetTextureOrCreateFromResource("ICON_SUDOKU", ICON_SUDOKU, hSelf);
			ButtonHover = APIDefs->GetTextureOrCreateFromResource("ICON_SUDOKU_HOVER", ICON_SUDOKU_HOVER, hSelf);
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.f, 0.f });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.f, 0.f });

			if (isEditingPosition)
			{
				ImGui::Image(IsSlashGGButtonHovered ? ButtonHover->Resource : Button->Resource, ImVec2(40.0f * NexusLink->Scaling, 40.0f * NexusLink->Scaling));
			}
			else if (ImGui::ImageButton(IsSlashGGButtonHovered ? ButtonHover->Resource : Button->Resource, ImVec2(40.0f * NexusLink->Scaling, 40.0f * NexusLink->Scaling)))
			{
				DoGG = true;
			}
			IsSlashGGButtonHovered = ImGui::IsItemHovered();
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);

			if (ImGui::BeginPopupContextItem("SlashGGCtxMenu"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0.f, 0.f }); // smol checkbox
				
				ImGui::Checkbox("Edit Position", &isEditingPosition);

				ImGui::PopStyleVar();

				ImGui::EndPopup();
			}
			ImGui::OpenPopupOnItemClick("SlashGGCtxMenu", 1);
		}
	}
	ImGui::End();

	if (isEditingPosition)
	{
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}
}
void AddonOptions()
{
	ImGui::Text("Open Chat Keybind:");
	ImGui::SameLine();
	if (ImGui::Button((KeybindToString(OpenChatKeybind, true) + "##OpenChat").c_str()))
	{
		ImGui::OpenPopup("Set Keybind: Open Chat", ImGuiPopupFlags_AnyPopupLevel);
	}
	ImGui::TooltipGeneric("This should match whatever keybind you're using in-game for \"Chat Message\".\n");

	if (ImGui::Checkbox("Visible##BTN_SUDOKU_VISIBLE", &IsSlashGGButtonVisible))
	{
		SaveSettings(SettingsPath);
	}

	ImGui::Text("The GG button will only show in instances e.g. Fractals, Raids, Strikes.");
	ImGui::Text("You can right-click the GG button to edit its position or enable the editing mode from here.");
	ImGui::Checkbox("Edit Mode##BTN_SUDOKU_EDIT", &isEditingPosition);

	if (ImGui::BeginPopupModal("Set Keybind: Open Chat"))
	{
		isSettingKeybind = true;
		if (CurrentKeybind == Keybind{})
		{
			ImGui::Text(KeybindToString(OpenChatKeybind, true).c_str());
		}
		else
		{
			ImGui::Text(KeybindToString(CurrentKeybind, true).c_str());
		}

		bool close = false;

		if (ImGui::Button("Unbind"))
		{
			OpenChatKeybind = {};
			close = true;
		}

		/* i love imgui */
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		/* i love imgui end*/

		if (ImGui::Button("Accept"))
		{
			OpenChatKeybind = CurrentKeybind;
			close = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			close = true;
		}

		if (close)
		{
			CurrentKeybind = Keybind{};
			isSettingKeybind = false;
			SaveSettings(SettingsPath);
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

void PerformSudoku()
{
	std::lock_guard<std::mutex> lock(GGMutex);
	for (;;)
	{
		while (!DoGG)
		{
			Sleep(1);
		}

		if (!IsGGThreadRunning)
		{
			break;
		}

		/*if (OpenClipboard(Game))
		{
			bufferPrevious = (char*)GetClipboardData(CF_TEXT);

			HGLOBAL clipbuffer = GlobalAlloc(GMEM_DDESHARE, 4);
			char* buffer = (char*)GlobalLock(clipbuffer);
			strcpy_s(buffer, 4, LPCSTR(source));
			GlobalUnlock(clipbuffer);
			SetClipboardData(CF_TEXT, clipbuffer);
			CloseClipboard();
		}*/

		std::string cbPrevious;
		size_t lenPrevious = 0;
		char source[4] = "/gg";
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 4);
		if (hMem)
		{
			LPVOID memLock = GlobalLock(hMem);
			if (memLock)
			{
				memcpy(memLock, source, 4);
				GlobalUnlock(hMem);
				if (OpenClipboard(Game))
				{
					HANDLE cbHandleOld = GetClipboardData(CF_TEXT);
					cbPrevious = (char*)GlobalLock(cbHandleOld);
					GlobalUnlock(cbHandleOld);
					lenPrevious = cbPrevious.size() + 1;
					EmptyClipboard();
					SetClipboardData(CF_TEXT, hMem);
					CloseClipboard();
				}
			}
		}

		if (!MumbleLink->Context.IsTextboxFocused && OpenChatKeybind == Keybind{}) /* fallback*/
		{
			PostMessage(Game, WM_KEYDOWN, VK_RETURN, GetLPARAM(VK_RETURN, 1, 0));
			PostMessage(Game, WM_KEYUP, VK_RETURN, GetLPARAM(VK_RETURN, 0, 0)); Sleep(15);
		}
		else if (!MumbleLink->Context.IsTextboxFocused)
		{
			if (OpenChatKeybind.Alt)
			{
				PostMessage(Game, WM_SYSKEYDOWN, VK_MENU, GetLPARAM(VK_MENU, 1, 1)); Sleep(15);
			}

			if (OpenChatKeybind.Shift)
			{
				PostMessage(Game, WM_KEYDOWN, VK_SHIFT, GetLPARAM(VK_SHIFT, 1, 0)); Sleep(15);
			}

			if (OpenChatKeybind.Ctrl)
			{
				PostMessage(Game, WM_KEYDOWN, VK_CONTROL, GetLPARAM(VK_CONTROL, 1, 0)); Sleep(15);
			}

			if (OpenChatKeybind.Key)
			{
				PostMessage(Game, WM_KEYDOWN, MapVirtualKey(OpenChatKeybind.Key, MAPVK_VSC_TO_VK),
					GetLPARAM(MapVirtualKey(OpenChatKeybind.Key, MAPVK_VSC_TO_VK), 1, 0));
			}

			if (OpenChatKeybind.Key)
			{
				PostMessage(Game, WM_KEYUP, MapVirtualKey(OpenChatKeybind.Key, MAPVK_VSC_TO_VK),
					GetLPARAM(MapVirtualKey(OpenChatKeybind.Key, MAPVK_VSC_TO_VK), 0, 0)); Sleep(15);
			}

			if (OpenChatKeybind.Ctrl)
			{
				PostMessage(Game, WM_KEYUP, VK_CONTROL, GetLPARAM(VK_CONTROL, 0, 0)); Sleep(15);
			}

			if (OpenChatKeybind.Shift)
			{
				PostMessage(Game, WM_KEYUP, VK_SHIFT, GetLPARAM(VK_SHIFT, 0, 0)); Sleep(15);
			}

			if (OpenChatKeybind.Alt)
			{
				PostMessage(Game, WM_SYSKEYUP, VK_MENU, GetLPARAM(VK_MENU, 0, 1)); Sleep(15);
			}
		}

		INPUT inputs1[1] = {};
		INPUT inputs2[3] = {};
		INPUT inputs3[1] = {};

		inputs1[0].type = INPUT_KEYBOARD;
		inputs1[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
		inputs1[0].ki.wVk = VK_LCONTROL;

		inputs2[0].type = INPUT_KEYBOARD;
		inputs2[0].ki.wScan = MapVirtualKey('A', MAPVK_VK_TO_VSC);
		inputs2[0].ki.wVk = 'A';

		inputs2[1].type = INPUT_KEYBOARD;
		inputs2[1].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
		inputs2[1].ki.wVk = 'V';

		inputs2[2].type = INPUT_KEYBOARD;
		inputs2[2].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
		inputs2[2].ki.wVk = 'V';
		inputs2[2].ki.dwFlags = KEYEVENTF_KEYUP;

		inputs3[0].type = INPUT_KEYBOARD;
		inputs3[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
		inputs3[0].ki.wVk = VK_LCONTROL;
		inputs3[0].ki.dwFlags = KEYEVENTF_KEYUP;

		UINT uSent1 = SendInput(ARRAYSIZE(inputs1), inputs1, sizeof(INPUT)); Sleep(15);
		UINT uSent2 = SendInput(ARRAYSIZE(inputs2), inputs2, sizeof(INPUT)); Sleep(15);
		UINT uSent3 = SendInput(ARRAYSIZE(inputs3), inputs3, sizeof(INPUT));

		/*Sleep(15);
		for (int i = 0; i < strlen(source); i++)
		{
			PostMessage(Game, WM_CHAR, (WPARAM)source[i], 0); Sleep(15);
		}*/

		PostMessage(Game, WM_KEYDOWN, VK_RETURN, GetLPARAM(VK_RETURN, 1, 0));
		PostMessage(Game, WM_KEYUP, VK_RETURN, GetLPARAM(VK_RETURN, 0, 0));

		if (!cbPrevious.empty())
		{
			HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, lenPrevious);
			if (hMem)
			{
				LPVOID memLock = GlobalLock(hMem);
				if (memLock)
				{
					memcpy(memLock, cbPrevious.c_str(), lenPrevious - 1);
					GlobalUnlock(hMem);
					if (OpenClipboard(Game))
					{
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hMem);
						CloseClipboard();
					}
				}
			}
		}

		DoGG = false;
	}
}

void LoadSettings(std::filesystem::path aPath)
{
	if (!std::filesystem::exists(aPath))
	{
		OpenChatKeybind.Key = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
		return;
	}

	Mutex.lock();
	{
		try
		{
			std::ifstream file(aPath);
			Settings = json::parse(file);
			file.close();
		}
		catch (json::parse_error& ex)
		{
			APIDefs->Log(ELogLevel_WARNING, "Restore Honor", "Settings.json could not be parsed.");
			APIDefs->Log(ELogLevel_WARNING, "Restore Honor", ex.what());
		}
	}
	Mutex.unlock();

	if (!Settings.is_null())
	{
		if (!Settings["OC_KEY"].is_null()) { Settings["OC_KEY"].get_to(OpenChatKeybind.Key); }
		if (!Settings["OC_ALT"].is_null()) { Settings["OC_ALT"].get_to(OpenChatKeybind.Alt); }
		if (!Settings["OC_CTRL"].is_null()) { Settings["OC_CTRL"].get_to(OpenChatKeybind.Ctrl); }
		if (!Settings["OC_SHIFT"].is_null()) { Settings["OC_SHIFT"].get_to(OpenChatKeybind.Shift); }
		if (!Settings["IsVisible"].is_null()) { Settings["IsVisible"].get_to(IsSlashGGButtonVisible); }
	}
	else
	{
		OpenChatKeybind.Key = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
	}
}
void SaveSettings(std::filesystem::path aPath)
{
	Settings["OC_KEY"] = OpenChatKeybind.Key;
	Settings["OC_ALT"] = OpenChatKeybind.Alt;
	Settings["OC_CTRL"] = OpenChatKeybind.Ctrl;
	Settings["OC_SHIFT"] = OpenChatKeybind.Shift;
	Settings["IsVisible"] = IsSlashGGButtonVisible;

	Mutex.lock();
	{
		std::ofstream file(aPath);
		file << Settings.dump(1, '\t') << std::endl;
		file.close();
	}
	Mutex.unlock();
}