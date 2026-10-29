#pragma once

#include "GameScript.h"
#include <cstddef>
#include <string>
#include <vector>

// classic OBSE's format string parameters: the string and up to 20 numbers
extern ParamInfo kParams_FormatString[21];

// Reads a command's format string and its numbers and expands it as classic OBSE does, with its
// own specifiers (%n for a form's name, %i for its ID, ...). The numbers the format string doesn't
// use go in rest, for commands that take more after them. False if the arguments don't fit.
bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out, std::vector <double> * rest = nullptr);

// the text of the string variable with this ID, or none (Commands_String.cpp)
const std::string * LookupStringVar(u32 id);

// redirects the game's calls the string variables need (Commands_String.cpp)
void InstallStringVarHooks();

// the parse handler of commands whose arguments classic OBSE compiles with its own expression
// compiler, which isn't ported: they run in compiled scripts but can't be typed in the console
bool Cmd_Expression_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf);

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
struct ScriptRunner
{
	u8		unk00[0x28];	// 00
	Script	* script;		// 28 the script running, 0 when idle
	u32		unk30;			// 30
	u32		unk34;			// 34
	s32		ifDepth;		// 38
	u32		ifStack[10];	// 3C
};

static_assert(offsetof(ScriptRunner, script) == 0x28);
static_assert(offsetof(ScriptRunner, ifDepth) == 0x38);
static_assert(offsetof(ScriptRunner, ifStack) == 0x3C);

// The runner running this statement, and Run's length for it, which the command can change to
// jump: the next statement is at argsOffset + *length (Commands_Classic.cpp, with Goto). False
// unless the command is a statement of its own run by the main runner: not part of an expression,
// nor in a script that another one started. returnAddress is the command's _ReturnAddress().
bool GetStatementState(void * returnAddress, Script * script, const char * scriptData, u32 argsOffset,
	u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length);

// ends the loops scripts left unfinished, as at the start of each frame and when a game is loaded
// (Commands_Loops.cpp)
void ResetLoops();

// a call at src to the function at dst, as the game was built
inline bool IsCallTo(uintptr_t src, uintptr_t dst)
{
	return *(const u8 *)src == 0xE8 && src + 5 + *(const s32 *)(src + 1) == dst;
}
