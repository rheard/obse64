#pragma once

#include "GameScript.h"
#include <cstddef>
#include <string>
#include <vector>

// the text of the string variable with this ID, or none (Commands_String.cpp)
const std::string * LookupStringVar(u32 id);

// redirects the game's calls the string variables need (Commands_String.cpp)
void InstallStringVarHooks();

// the parse handler of commands whose arguments classic OBSE compiles with its own expression
// compiler, which isn't ported: they run in compiled scripts but can't be typed in the console
bool Cmd_Expression_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf);

// IsWeapon and the rest of that family, and IsCreature: whether the form given, or the calling
// reference's base without one, is of this type (Commands_Inventory.cpp)
bool IsFormType_Execute(COMMAND_ARGS, u8 typeID, const char * name, bool anyForm);

// redirects the game's item messages, so AddItemNS and the rest can skip them
// (Commands_Inventory.cpp)
void InstallItemMessageHook();

// makes the game's IsRunning answer as classic's did for the player on foot
// (Commands_Player.cpp)
void InstallIsRunning();

// notes each save loaded and new game started, for GetGameLoaded (Commands_Game.cpp)
void InstallGameLoadedHooks();

// how many games have been loaded or started: a number that changes with each, for anything that
// must not outlive the game it was made in (Commands_Game.cpp)
u32 GetGameLoadCount();

// whether a control (classic's number) is down in the game's controls
bool IsControlDown(u32 control);

// whether a key (classic's DirectInput scan code, or from 256 a mouse button) is down while the
// game's window is in front (Commands_Input.cpp)
bool IsKeycodeDown(u32 keycode);

// sends the frame's key and control events to their handlers (Commands_Events.cpp)
void ProcessInputEvents();

// does the key presses scripts asked for, once each Unreal frame, on its game thread
// (Commands_Input.cpp)
void UpdateKeyInput();

// The game's script runner (0xC0 bytes, built by 0x0694C7C0). ScriptRunner::Run (0x0694D6D0) steps
// through a script's statements and has 0x0694C810 run each one. The runner keeps the if/else
// nesting: a depth, and for each level 1 while inside an if and 2 once one of its branches ran.
// Nothing bounds the depth: an If past the tenth level writes past ifStack.
struct ScriptRunner
{
	TESObjectREFR	* containingObj;	// 00
	u64				unk08;				// 08
	ScriptLocals	* locals;			// 10 the locals the script runs with, which its commands are given
	u8				unk18[0x10];		// 18
	Script			* script;			// 28 the script running, 0 when idle
	u32				unk30;				// 30
	u32				unk34;				// 34
	s32				ifDepth;			// 38
	u32				ifStack[10];		// 3C
};

static_assert(offsetof(ScriptRunner, locals) == 0x10);
static_assert(offsetof(ScriptRunner, script) == 0x28);
static_assert(offsetof(ScriptRunner, ifDepth) == 0x38);
static_assert(offsetof(ScriptRunner, ifStack) == 0x3C);

// whether a command was called as a statement of its own, by any runner, rather than from an
// expression; returnAddress is the command's _ReturnAddress()
bool IsStatementCall(void * returnAddress);

// The runner running this statement, and Run's length for it, which the command can change to
// jump: the next statement is at argsOffset + *length (Commands_Classic.cpp, with Goto). False
// unless the command is a statement of its own, not part of an expression, run by the innermost
// runner running, with this script and these locals. returnAddress is the command's
// _ReturnAddress().
bool GetStatementState(void * returnAddress, Script * script, ScriptLocals * locals, const char * scriptData,
	u32 argsOffset, u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length);

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
