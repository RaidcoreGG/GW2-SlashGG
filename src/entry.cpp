#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "imgui/imgui.h"
#include "ImPos/imgui_positioning.h"
#include "mumble/Mumble.h"
#include "nexus/Nexus.h"

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
bool RestoreClipboard = true;
Texture* Button = nullptr;
Texture* ButtonHover = nullptr;

std::mutex GGMutex;
std::thread GGThread;
bool IsGGThreadRunning = false;
bool DoGG = false;

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
	APIDefs->DeregisterRender(AddonOptions);
	APIDefs->DeregisterRender(AddonRender);

	MumbleLink = nullptr;
	NexusLink = nullptr;

	// this ends the thread, I'm too lazy to implement proper logic right now
	IsGGThreadRunning = false;
	DoGG = true;
	std::lock_guard<std::mutex> lock(GGMutex);
}

void ProcessKeybind(const char* aIdentifier)
{
	if (strcmp(aIdentifier, "KB_SUDOKU") == 0)
	{
		DoGG = true;
		return;
	}
}

void AddonRender()
{
	if (!NexusLink || !NexusLink->IsGameplay || !MumbleLink || MumbleLink->Context.IsMapOpen || !IsSlashGGButtonVisible || (MumbleLink->Context.MapType != Mumble::EMapType::Instance))
	{
		return;
	}

	if (ImGui::Begin("Sudoku!", (bool*)0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiExt::UpdatePosition("Sudoku!")))
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

			if (ImGui::ImageButton(IsSlashGGButtonHovered ? ButtonHover->Resource : Button->Resource, ImVec2(40.0f * NexusLink->Scaling, 40.0f * NexusLink->Scaling)))
			{
				DoGG = true;
			}
			IsSlashGGButtonHovered = ImGui::IsItemHovered();
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);
		}
	}
	ImGuiExt::ContextMenuPosition("SlashGGCtxMenu");

	ImGui::End();
}
void AddonOptions()
{
	if (ImGui::Checkbox("Visible##BTN_SUDOKU_VISIBLE", &IsSlashGGButtonVisible))
	{
		SaveSettings(SettingsPath);
	}

	if (ImGui::Checkbox("Restore Clipboard##BTN_SUDOKU_RESTORE", &RestoreClipboard))
	{
		SaveSettings(SettingsPath);
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("Resets the clipboard to its previous content after pasting /gg.");
		ImGui::EndTooltip();
	}

	ImGui::Text("The GG button will only show in instances e.g. Fractals, Raids, Strikes.");
	ImGui::Text("You can right-click the GG button to edit its position.");
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

		if (!MumbleLink->Context.IsTextboxFocused && MumbleLink->Context.MapType == Mumble::EMapType::Instance)
		{
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
						if (cbHandleOld)
						{
							LPVOID memLockOld = GlobalLock(cbHandleOld);
							if (memLockOld)
							{
								cbPrevious = (char*)memLockOld;
								lenPrevious = cbPrevious.size() + 1;
								GlobalUnlock(cbHandleOld);
							}
						}
						EmptyClipboard();
						SetClipboardData(CF_TEXT, hMem);
						CloseClipboard();
					}
				}
			}

			INPUT retPress[1] = {};
			INPUT retRelease[1] = {};

			retPress[0].type = INPUT_KEYBOARD;
			retPress[0].ki.wScan = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
			retPress[0].ki.wVk = VK_RETURN;

			retRelease[0].type = INPUT_KEYBOARD;
			retRelease[0].ki.wScan = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
			retRelease[0].ki.wVk = VK_RETURN;
			retRelease[0].ki.dwFlags = KEYEVENTF_KEYUP;

			INPUT lctrlPress[1] = {};
			INPUT lctrlRelease[1] = {};

			lctrlPress[0].type = INPUT_KEYBOARD;
			lctrlPress[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
			lctrlPress[0].ki.wVk = VK_LCONTROL;

			lctrlRelease[0].type = INPUT_KEYBOARD;
			lctrlRelease[0].ki.wScan = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);
			lctrlRelease[0].ki.wVk = VK_LCONTROL;
			lctrlRelease[0].ki.dwFlags = KEYEVENTF_KEYUP;

			INPUT vPress[1] = {};
			INPUT vRelease[1] = {};

			vPress[0].type = INPUT_KEYBOARD;
			vPress[0].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
			vPress[0].ki.wVk = 'V';

			vRelease[0].type = INPUT_KEYBOARD;
			vRelease[0].ki.wScan = MapVirtualKey('V', MAPVK_VK_TO_VSC);
			vRelease[0].ki.wVk = 'V';
			vRelease[0].ki.dwFlags = KEYEVENTF_KEYUP;

			/* return stroke  */
			SendInput(ARRAYSIZE(retPress), retPress, sizeof(INPUT));
			SendInput(ARRAYSIZE(retRelease), retRelease, sizeof(INPUT));

			int wait = 0;
			while (!MumbleLink->Context.IsTextboxFocused)
			{
				Sleep(1);
				wait++;

				if (wait >= 50)
				{
					break;
				}
			}

			/* continue loop */
			if (wait < 50)
			{
				/* lctrl press */
				SendInput(ARRAYSIZE(lctrlPress), lctrlPress, sizeof(INPUT));

				/* v stroke */
				SendInput(ARRAYSIZE(vPress), vPress, sizeof(INPUT));
				SendInput(ARRAYSIZE(vRelease), vRelease, sizeof(INPUT));

				Sleep(50);

				/* lctrl release */
				SendInput(ARRAYSIZE(lctrlRelease), lctrlRelease, sizeof(INPUT));

				/* return stroke  */
				SendInput(ARRAYSIZE(retPress), retPress, sizeof(INPUT));
				SendInput(ARRAYSIZE(retRelease), retRelease, sizeof(INPUT));
			}

			if (RestoreClipboard)
			{
				wait = 0; /* wait for x ms */
				int delay = 50; /* wait at least 50 ms initially. */
				do
				{
					Sleep(delay);
					delay = 1;
					wait++;

					if (wait >= 250)
					{
						break;
					}
				} while (MumbleLink->Context.IsTextboxFocused);

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
			}
		}

		DoGG = false;
	}
}

void LoadSettings(std::filesystem::path aPath)
{
	if (!std::filesystem::exists(aPath))
	{
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
		if (!Settings["IsVisible"].is_null()) { Settings["IsVisible"].get_to(IsSlashGGButtonVisible); }
		if (!Settings["RestoreClipboard"].is_null()) { Settings["RestoreClipboard"].get_to(RestoreClipboard); }
	}
}
void SaveSettings(std::filesystem::path aPath)
{
	Settings["IsVisible"] = IsSlashGGButtonVisible;
	Settings["RestoreClipboard"] = RestoreClipboard;

	Mutex.lock();
	{
		std::ofstream file(aPath);
		file << Settings.dump(1, '\t') << std::endl;
		file.close();
	}
	Mutex.unlock();
}