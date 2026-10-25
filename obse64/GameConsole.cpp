#include "obse64/GameConsole.h"
#include "obse64_common/Log.h"
#include "obse64_common/Relocation.h"
#include "obse64_common/Utilities.h"

RelocPtr <u8> g_bConsoleMode(0x094800B8);
RelocPtr <std::uintptr_t> _Console_Print(0x0656A7A0);

// Console_Print prints only while this is set, and the game sets it only around a typed command
// (RVA 0x06555280), so lines printed by running scripts are dropped.
RelocPtr <u8> g_bConsoleOutputEnabled(0x09480180);

bool IsConsoleMode()
{
	return *g_bConsoleMode;
}

void ScriptConsolePrint(const char * text)
{
	static int s_logPrints = -1;
	if(s_logPrints < 0)
	{
		u32 value = 1;
		getConfigOption_u32("Debug", "LogScriptPrints", &value);
		s_logPrints = value ? 1 : 0;
	}

	u8 outputEnabled = *g_bConsoleOutputEnabled;
	*g_bConsoleOutputEnabled = 1;
	Console_Print("%s", text);
	*g_bConsoleOutputEnabled = outputEnabled;

	if(s_logPrints)
		_MESSAGE("console: %s", text);
}
