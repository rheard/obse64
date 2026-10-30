#pragma once

#include "GameScript.h"
#include <string>

// What the files of classic OBSE's commands share, and the hooks AddClassicCommands installs.

// the text of the string variable with this ID, or none (Commands_String.cpp)
const std::string * LookupStringVar(u32 id);

// redirects the game's calls the string variables need (Commands_String.cpp)
void InstallStringVarHooks();

// IsWeapon and the rest of that family, and IsCreature: whether the form given, or the calling
// reference's base without one, is of this type (Commands_Inventory.cpp)
bool IsFormType_Execute(COMMAND_ARGS, u8 typeID, const char * name, bool anyForm);

// redirects the game's item messages, so AddItemNS and the rest can skip them
// (Commands_Inventory.cpp)
void InstallItemMessageHook();

// makes the game's IsRunning answer as classic's did for the player on foot
// (Commands_Player.cpp)
void InstallIsRunning();

// notes the item the magic menu shows, for GetActiveMenuSelection (Commands_Menu.cpp)
void InstallMagicMenuHooks();

// notes each save loaded and new game started, for GetGameLoaded (Commands_Game.cpp)
void InstallGameLoadedHooks();

// how many games have been loaded or started: a number that changes with each, for anything that
// must not outlive the game it was made in (Commands_Game.cpp)
u32 GetGameLoadCount();

// whether a control (classic's number) is down in the game's controls (Commands_Input.cpp)
bool IsControlDown(u32 control);

// whether a key (classic's DirectInput scan code, or from 256 a mouse button) is down while the
// game's window is in front (Commands_Input.cpp)
bool IsKeycodeDown(u32 keycode);

// sends the frame's key and control events to their handlers (Commands_Events.cpp)
void ProcessInputEvents();

// does the key presses scripts asked for, once each Unreal frame, on its game thread
// (Commands_Input.cpp)
void UpdateKeyInput();

// whether a command was called as a statement of its own, by any runner, rather than from an
// expression; returnAddress is the command's _ReturnAddress() (Commands_Loops.cpp)
bool IsStatementCall(void * returnAddress);

// notes the runner each script runs in, for Goto and the loops (Commands_Loops.cpp)
void InstallRunnerHooks();

// ends the loops scripts left unfinished, as at the start of each frame and when a game is loaded
// (Commands_Loops.cpp)
void ResetLoops();

// a call at src to the function at dst, as the game was built
inline bool IsCallTo(uintptr_t src, uintptr_t dst)
{
	return *(const u8 *)src == 0xE8 && src + 5 + *(const s32 *)(src + 1) == dst;
}

// a jump at src to the function at dst, as the game was built (a call in a function's tail)
inline bool IsJumpTo(uintptr_t src, uintptr_t dst)
{
	return *(const u8 *)src == 0xE9 && src + 5 + *(const s32 *)(src + 1) == dst;
}
