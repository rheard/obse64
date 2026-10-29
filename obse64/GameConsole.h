#pragma once

#include "obse64_common/Relocation.h"
#include "obse64_common/Utilities.h"

extern RelocPtr <std::uintptr_t> _Console_Print;

template <class... Args>
void Console_Print(const char* fmt, Args... args)
{
	using func_t = void(*)(const char*, ...);
	auto func = reinterpret_cast<func_t>(_Console_Print.getPtr());
	return func(fmt, args...);
}

bool IsConsoleMode();

// set while a command typed into the console runs
extern RelocPtr <u8> g_bConsoleMode;

// Console_Print prints only while this is set
extern RelocPtr <u8> g_bConsoleOutputEnabled;

// Prints a line from a script command to the console, turning on the console output the game
// otherwise allows only while a typed command runs, and copies the line to obse64.txt.
// [Debug] LogScriptPrints=0 in obse.ini turns the copy off.
void ScriptConsolePrint(const char * text);
