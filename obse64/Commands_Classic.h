#pragma once

#include "GameScript.h"
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

// a call at src to the function at dst, as the game was built
inline bool IsCallTo(uintptr_t src, uintptr_t dst)
{
	return *(const u8 *)src == 0xE8 && src + 5 + *(const s32 *)(src + 1) == dst;
}
