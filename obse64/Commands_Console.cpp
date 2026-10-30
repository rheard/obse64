#include "Commands_Classic.h"
#include "FormatString.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameScript.h"
#include "ObseExpression.h"
#include "obse64_common/Log.h"
#include "obse64_common/Utilities.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <Windows.h>

/* Print formatted string to Oblivion console
 * syntax: PrintToConsole fmtstring num1 num2 ...
 * shortname: printc
 *
 * Prints a formatted string to the Oblivion console, as classic OBSE does.
 * -Numbers use printf notation (%#.#f %g %e), and OBSE's own specifiers work too, such as %n for a
 *  form's name and %i for its ID, so classic mods' format strings print as they did.
 * -You can pass up to 20 variables, but you don't have to pass any at all.
 * -Returns 1 if it printed.
 */
bool Cmd_PrintToConsole_Execute(COMMAND_ARGS)
{
	*result = 0;

	// not snprintf: classic mods pass OBSE's specifiers, and snprintf rejects %n, which ends the game
	std::string text;
	if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text))
	{
		ScriptConsolePrint(text.c_str());
		*result = 1;
	}

	return true;
}

CommandInfo kCommandInfo_PrintToConsole =
{
	"PrintToConsole", "printc",
	0,
	"Print formatted string to console",
	0,
	21, kParams_FormatString,
	Cmd_PrintToConsole_Execute
};

// classic OBSE's console commands: debug output, expressions printed and tested, and console
// lines run from a file (Commands_Classic.cpp registers them)

// debug output, switched on per mod by SetDebugMode
static u32 s_modDebugStates[8];

static u8 GetScriptModIndex(Script * script)
{
	// Script derives from TESForm
	return script ? u8(reinterpret_cast <TESForm *>(script)->refID >> 24) : 0xFF;
}

static bool IsDebugModeOn(Script * script)
{
	u8 modIndex = GetScriptModIndex(script);
	return (s_modDebugStates[modIndex / 32] & (1 << (modIndex % 32))) != 0;
}

static bool Cmd_SetDebugMode_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 enable = 0;
	u32 modIndexArg = 0xFFFF;
	if(!ExtractArgs(EXTRACT_ARGS, &enable, &modIndexArg)) return true;

	u8 modIndex = (modIndexArg == 0xFFFF) ? GetScriptModIndex(script) : u8(modIndexArg);
	if(modIndex > 0 && modIndex < 0xFF)
	{
		u32 bit = 1 << (modIndex % 32);
		if(enable)
			s_modDebugStates[modIndex / 32] |= bit;
		else
			s_modDebugStates[modIndex / 32] &= ~bit;

		if(IsConsoleMode())
			Console_Print("Debug statements toggled %s for mod %02X", enable ? "on" : "off", modIndex);
	}

	return true;
}

static bool Cmd_DebugPrint_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(IsDebugModeOn(script))
	{
		std::string text;
		if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text))
			ScriptConsolePrint(text.c_str());
	}

	return true;
}

static bool Cmd_PrintD_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(IsDebugModeOn(script))
	{
		ObseExpression eval(PASS_COMMAND_ARGS);
		if(eval.ExtractArgs() && eval.NumArgs() && eval.Arg(0).type == ObseExpression::Value::kType_String)
			ScriptConsolePrint(eval.Arg(0).string.c_str());
	}

	return true;
}

static bool Cmd_TestExpr_Execute(COMMAND_ARGS)
{
	*result = 0;

	ObseExpression eval(PASS_COMMAND_ARGS);
	if(eval.ExtractArgs() && eval.NumArgs())
		*result = 1;

	return true;
}

static ParamInfo kParams_OneInt_OneOptionalInt[2] =
{
	{	"int", kParamType_Integer, 0 },
	{	"int", kParamType_Integer, 1 },
};

CommandInfo kCommandInfo_SetDebugMode =
{
	"SetDebugMode", "",
	0,
	"toggles debug mode for all scripts belonging to a mod",
	0,
	2, kParams_OneInt_OneOptionalInt,
	Cmd_SetDebugMode_Execute
};

CommandInfo kCommandInfo_DebugPrint =
{
	"DebugPrint", "",
	0,
	"prints a formatted string to the console if debug mode is enabled for the calling script",
	0,
	21, kParams_FormatString,
	Cmd_DebugPrint_Execute
};

CommandInfo kCommandInfo_PrintD =
{
	"PrintD", "",
	0,
	"prints a string expression to the console if debug mode is enabled for the calling script",
	0,
	0, nullptr,
	Cmd_PrintD_Execute,
	Cmd_Expression_Parse
};

CommandInfo kCommandInfo_TestExpr =
{
	"TestExpr", "",
	0,
	"returns 1 if the expression evaluates without an error",
	0,
	0, nullptr,
	Cmd_TestExpr_Execute,
	Cmd_Expression_Parse
};


// The console runs a typed line as classic's RunScriptLine did (0x06555280): a temporary script
// (built by 0x0662E4F0, marked temporary by 0x06612830) takes the line as its text (0x066315B0)
// and is compiled and run (0x0662EBE0) by the compiler at the start of the console manager (from
// 0x066331E0), then destroyed (0x0662E5A0). A line that doesn't compile leaves the script without
// bytecode: its length at +0x38 stays 0. Running it sets and clears the console mode flag.
typedef void (* _Script_Construct)(void * script);
static RelocAddr <_Script_Construct> Script_Construct(0x0662E4F0);

typedef void (* _Script_MarkAsTemporary)(void * script);
static RelocAddr <_Script_MarkAsTemporary> Script_MarkAsTemporary(0x06612830);

typedef void (* _Script_SetText)(void * script, const char * text);
static RelocAddr <_Script_SetText> Script_SetText(0x066315B0);

typedef void (* _Script_CompileAndRun)(void * script, void * compiler, u32 compilerType, TESObjectREFR * callingObj);
static RelocAddr <_Script_CompileAndRun> Script_CompileAndRun(0x0662EBE0);

typedef void (* _Script_Destruct)(void * script);
static RelocAddr <_Script_Destruct> Script_Destruct(0x0662E5A0);

typedef void ** (* _ConsoleManager_Get)(bool create);
static RelocAddr <_ConsoleManager_Get> ConsoleManager_Get(0x066331E0);

enum
{
	kScriptCompiler_Console =	1,

	kScriptOffset_DataLength =	0x38,
	kScriptBufferSize =			0x100,	// the console keeps its script in 0x98 bytes
};

// compiles and runs one line as the console does; false if it didn't compile
static bool RunScriptLine(const char * text, TESObjectREFR * callingObj)
{
	void ** consoleManager = ConsoleManager_Get(true);
	if(!consoleManager || !*consoleManager) return false;

	alignas(16) u8 scriptBuf[kScriptBufferSize];
	u8 consoleMode = *g_bConsoleMode;

	Script_Construct(scriptBuf);
	Script_MarkAsTemporary(scriptBuf);
	Script_SetText(scriptBuf, text);
	Script_CompileAndRun(scriptBuf, *consoleManager, kScriptCompiler_Console, callingObj);
	bool compiled = *(const u32 *)(scriptBuf + kScriptOffset_DataLength) != 0;
	Script_Destruct(scriptBuf);

	*g_bConsoleMode = consoleMode;

	return compiled;
}

static ParamInfo kParams_RunBatchScript[3] =
{
	{	"filename",			kParamType_String,	0 },
	{	"run on ref",		kParamType_Integer,	1 },
	{	"suppress output",	kParamType_Integer,	1 },
};

// Runs each line of a text file as a console command, as classic did. Classic opened the file
// relative to Oblivion's folder, the one holding Data; the Remastered's is Content\Dev\ObvData,
// two folders up from the exe's, and a path is tried there first. Blank lines and comments are
// skipped instead of being compiled to nothing.
static bool Cmd_RunBatchScript_Execute(COMMAND_ARGS)
{
	*result = 0;

	char fileName[BUFSIZ] = { 0 };
	u32 runOnRef = 0;
	u32 suppressOutput = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &fileName, &runOnRef, &suppressOutput)) return true;

	std::string inDataFolder = getRuntimeDirectory() + "..\\..\\Content\\Dev\\ObvData\\" + fileName;

	FILE * src = nullptr;
	if(fopen_s(&src, inDataFolder.c_str(), "r") && fopen_s(&src, fileName, "r"))
	{
		_MESSAGE("RunBatchScript: can't open %s", fileName);
		return true;
	}

	*result = 1;

	// classic printed what the lines print, unless asked not to
	u8 outputEnabled = *g_bConsoleOutputEnabled;
	*g_bConsoleOutputEnabled = suppressOutput ? 0 : 1;

	char line[4096];
	while(fgets(line, sizeof(line), src))
	{
		size_t len = strlen(line);
		while(len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = 0;

		const char * start = line;
		while(*start == ' ' || *start == '\t')
			start++;

		if(!*start || *start == ';')
			continue;

		if(!RunScriptLine(start, runOnRef ? thisObj : nullptr))
			*result = 0;
	}

	*g_bConsoleOutputEnabled = outputEnabled;

	fclose(src);

	return true;
}

// Classic asked the game's file finder, relative to Oblivion's folder, which also looks in the
// BSA archives. This looks for a loose file only, in Content\Dev\ObvData first, as RunBatchScript
// opens one.
static bool Cmd_FileExists_Execute(COMMAND_ARGS)
{
	*result = 0;

	char fileName[BUFSIZ] = { 0 };
	if(!ExtractArgs(EXTRACT_ARGS, &fileName)) return true;

	std::string inDataFolder = getRuntimeDirectory() + "..\\..\\Content\\Dev\\ObvData\\" + fileName;

	for(const char * path : { inDataFolder.c_str(), (const char *)fileName })
	{
		DWORD attributes = GetFileAttributesA(path);
		if(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			*result = 1;
			break;
		}
	}

	if(IsConsoleMode())
		Console_Print("FileExists >> %d", (int)*result);

	return true;
}

static ParamInfo kParams_OneString[1] =
{
	{	"string", kParamType_String, 0 },
};

CommandInfo kCommandInfo_FileExists =
{
	"FileExists", "",
	0,
	"returns true if the specified file exists",
	0,
	1, kParams_OneString,
	Cmd_FileExists_Execute
};

CommandInfo kCommandInfo_RunBatchScript =
{
	"RunBatchScript", "",
	0,
	"Similar to the 'bat' console command",
	0,
	3, kParams_RunBatchScript,
	Cmd_RunBatchScript_Execute
};
