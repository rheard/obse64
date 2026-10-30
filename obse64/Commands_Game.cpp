#include "Commands_Classic.h"
#include "FormatString.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameMenus.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "GameTypes.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// classic OBSE's commands about the game: what the crosshair is on, whether a game was loaded or the
// game started, and messages for the player (Commands_Classic.cpp registers them)

// The interface manager keeps the reference the player would activate at +0x158. Unreal casts
// the player's line of sight and hands the hit over (0x0655A460); the setter (0x065D6950) stores
// it there while it's within reach and clears it otherwise, and the player's own activation reads
// it from there (0x065694B0). Classic read HUDInfoMenu::crosshairRef, which stays empty here.

static bool Cmd_GetCrosshairRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESObjectREFR * refr = nullptr;
	if(u8 * intfc = *g_interfaceManager)
	{
		if(auto * form = *(TESForm **)(intfc + 0x158))
			refr = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
	}

	if(refr)
		SetFormResult(result, refr->refID);

	if(IsConsoleMode())
		Console_Print("GetCrosshairRef >> %08X (%s)", refr ? refr->refID : 0, refr ? GetFullName(refr) : "nothing");

	return true;
}

CommandInfo kCommandInfo_GetCrosshairRef =
{
	"GetCrosshairRef", "",
	0,
	"returns the reference under the crosshair",
	0,
	0, nullptr,
	Cmd_GetCrosshairRef_Execute
};

// GetGameLoaded is 1 the first time each script asks after a save is loaded or a new game starts,
// and, as in classic, the first time after the game itself starts. The game's loader (0x0670FA80)
// opens the save with 0x06715190 and gives up if that fails. A new game closes the main menu
// (0x065DE980) from both of its starts, after the confirmation (0x065DEA90) or without one
// (0x065DEC40), once it has found the character generation quest.
typedef void * (* _SaveLoad_OpenSave)(void * saveLoad, void * unk, const char * name, s32 unk2);
static RelocAddr <_SaveLoad_OpenSave> SaveLoad_OpenSave(0x06715190);
static RelocAddr <uintptr_t> kLoadGame_OpenSaveCall(0x0670FB9C);

typedef void (* _MainMenu_Close)();
static RelocAddr <_MainMenu_Close> MainMenu_Close(0x065DE980);
static RelocAddr <uintptr_t> kNewGame_CloseMainMenuCall(0x065DEB5F);
static RelocAddr <uintptr_t> kNewGameUnconfirmed_CloseMainMenuCall(0x065DEDA7);

static bool s_gameLoaded = false;
static u32 s_gameLoadCount = 0;	// for anything that must not outlive the game it was made in

u32 GetGameLoadCount()
{
	return s_gameLoadCount;
}

static void OnGameLoaded()
{
	s_gameLoaded = true;
	s_gameLoadCount++;

	// before the save's forms come in, as an unfinished loop's inventory reference has a form ID
	ResetLoops();
}

static void * SaveLoad_OpenSave_Hook(void * saveLoad, void * unk, const char * name, s32 unk2)
{
	void * save = SaveLoad_OpenSave(saveLoad, unk, name, unk2);
	if(save)
		OnGameLoaded();

	return save;
}

static void MainMenu_Close_NewGame_Hook()
{
	OnGameLoaded();

	MainMenu_Close();
}

void InstallGameLoadedHooks()
{
	uintptr_t openSave = SaveLoad_OpenSave.getUIntPtr();
	uintptr_t closeMainMenu = MainMenu_Close.getUIntPtr();

	if(!IsCallTo(kLoadGame_OpenSaveCall.getUIntPtr(), openSave) ||
		!IsCallTo(kNewGame_CloseMainMenuCall.getUIntPtr(), closeMainMenu) ||
		!IsCallTo(kNewGameUnconfirmed_CloseMainMenuCall.getUIntPtr(), closeMainMenu))
	{
		_ERROR("the game's load and new game calls aren't where expected; GetGameLoaded will only report the game starting");
		return;
	}

	g_branchTrampoline.write5Call(kLoadGame_OpenSaveCall.getUIntPtr(), uintptr_t(SaveLoad_OpenSave_Hook));
	g_branchTrampoline.write5Call(kNewGame_CloseMainMenuCall.getUIntPtr(), uintptr_t(MainMenu_Close_NewGame_Hook));
	g_branchTrampoline.write5Call(kNewGameUnconfirmed_CloseMainMenuCall.getUIntPtr(), uintptr_t(MainMenu_Close_NewGame_Hook));
}

static bool Cmd_GetGameLoaded_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_informed;	// script form IDs told since the last load

	*result = 0;

	if(s_gameLoaded)
	{
		s_informed.clear();
		s_gameLoaded = false;
	}

	// Script derives from TESForm
	if(script && s_informed.insert(reinterpret_cast <TESForm *>(script)->refID).second)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("GetGameLoaded >> %d", (int)*result);

	return true;
}

// 1 the first time each script asks after the game starts, and never after a load
static bool Cmd_GetGameRestarted_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_informed;	// script form IDs told since the game started

	*result = 0;

	// Script derives from TESForm
	if(script && s_informed.insert(reinterpret_cast <TESForm *>(script)->refID).second)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("GetGameRestarted >> %d", (int)*result);

	return true;
}

CommandInfo kCommandInfo_GetGameLoaded =
{
	"GetGameLoaded", "",
	0,
	"returns 1 the first time the calling script asks after a game is loaded",
	0,
	0, nullptr,
	Cmd_GetGameLoaded_Execute
};

CommandInfo kCommandInfo_GetGameRestarted =
{
	"GetGameRestarted", "",
	0,
	"returns 1 the first time the calling script asks after the game starts",
	0,
	0, nullptr,
	Cmd_GetGameRestarted_Execute
};

// the callback MessageBoxEX gives a message box, which records the button pressed for
// GetButtonPressed
static RelocAddr <uintptr_t> kMessageBox_RecordButton(0x06A0DF40);

// what GetButtonPressed reads: the form ID of the script's calling reference (or of the script),
// and the button pressed, 0xFF until one is
static RelocPtr <u32> g_messageBoxFormID(0x094800BC);
static RelocPtr <u8> g_messageBoxButton(0x08FAB8F8);

enum
{
	kMaxMessageBoxButtons =	10,
	kRefFlag_Temporary =	0x4000,
};

static bool IsKeyChar(char c)
{
	return isalnum(u8(c)) || c == '_';
}

// text as FText::Format reads it literally
static void AppendFormatEscaped(std::string * out, const std::string & text, size_t start, size_t end)
{
	for(size_t i = start; i < end; i++)
	{
		if(strchr("{}`", text[i]))
			*out += '`';

		*out += text[i];
	}
}

// The text as a message for Unreal: each "LOC_" key in it ends a part, as that part's argument,
// so Unreal shows it translated.
static void BuildUIMessage(UIMessage * message, const std::string & text)
{
	auto addPart = [&](const std::string & pattern, const char * key)
	{
		GameStdString gamePattern = ToGameStdString(pattern.c_str());
		UIMessage_AddPart(message, &gamePattern, "Key", key);
	};

	std::string pattern;
	bool added = false;
	size_t pos = 0;
	for(size_t found = text.find("LOC_"); found != std::string::npos; found = text.find("LOC_", found + 1))
	{
		size_t end = found + 4;
		while(end < text.size() && IsKeyChar(text[end]))
			end++;

		if((found > 0 && IsKeyChar(text[found - 1])) || end == found + 4) continue;

		AppendFormatEscaped(&pattern, text, pos, found);
		pattern += "{Key}";
		addPart(pattern, text.substr(found, end - found).c_str());

		pattern.clear();
		added = true;
		pos = end;
		found = end - 1;
	}

	AppendFormatEscaped(&pattern, text, pos, text.size());
	if(!pattern.empty() || !added)
		addPart(pattern, "");
}

static bool Cmd_MessageEX_Execute(COMMAND_ARGS)
{
	*result = 0;

	std::string text;
	std::vector <double> rest;
	if(!ExtractClassicFormatString(PASS_COMMAND_ARGS, &text, &rest)) return true;

	// classic's optional duration in seconds follows the format string's numbers; without it, the
	// game's Message uses 10
	float duration = (!rest.empty() && rest[0] > 0) ? float(rest[0]) : 10.0f;

	UIMessage message = {};
	BuildUIMessage(&message, text);
	QueueUIMessage(&message, nullptr, 1, duration);
	UIMessage_DestroyParts(message.parts);

	*result = 1;

	return true;
}

static bool Cmd_MessageBoxEX_Execute(COMMAND_ARGS)
{
	*result = 0;

	std::string text;
	if(!ExtractClassicFormatString(PASS_COMMAND_ARGS, &text)) return true;

	// "message|button|button...", up to ten buttons. With none the game adds its own "Done", and an
	// empty one is left out, as the game would end the list of buttons there.
	size_t bar = text.find('|');
	std::string message = text.substr(0, bar);

	std::vector <std::string> buttons;
	while(bar != std::string::npos && buttons.size() < kMaxMessageBoxButtons)
	{
		size_t next = text.find('|', bar + 1);
		std::string button = text.substr(bar + 1, (next == std::string::npos) ? std::string::npos : next - bar - 1);
		if(!button.empty())
			buttons.push_back(button);

		bar = next;
	}

	// for GetButtonPressed, as the game's MessageBox does it: the calling reference, unless it's a
	// temporary one or there's none (a quest's script, an item's), and then the script
	if(thisObj && !(thisObj->flags & kRefFlag_Temporary))
		*g_messageBoxFormID = thisObj->refID;
	else if(script)
		*g_messageBoxFormID = reinterpret_cast <TESForm *>(script)->refID;	// Script derives from TESForm
	else
		return true;

	*g_messageBoxButton = 0xFF;

	const char * b[kMaxMessageBoxButtons + 1] = { };
	for(size_t i = 0; i < buttons.size(); i++)
		b[i] = buttons[i].c_str();

	if(buttons.empty())
		b[0] = "LOC_HC_MenuGamesettings_sDone";

	UIMessage box = {};
	BuildUIMessage(&box, message);
	ShowMessageBox(&box, kMessageBox_RecordButton.getUIntPtr(), 0, nullptr,
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
	UIMessage_DestroyParts(box.parts);

	return true;
}

CommandInfo kCommandInfo_MessageEX =
{
	"MessageEX", "",
	0,
	"Prints a formatted message",
	0,
	21, kParams_FormatString,
	Cmd_MessageEX_Execute
};

CommandInfo kCommandInfo_MessageBoxEX =
{
	"MessageBoxEX", "",
	0,
	"Displays a formatted messagebox",
	0,
	21, kParams_FormatString,
	Cmd_MessageBoxEX_Execute
};
