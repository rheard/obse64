#include "Commands_Classic.h"
#include "CommandTable.h"
#include "FormatString.h"
#include "FunctionScripts.h"
#include "GameConsole.h"
#include "GameExtraData.h"
#include "GameForms.h"
#include "GameMenus.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "PlayerControls.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <map>
#include <set>
#include <string>
#include <Windows.h>

// Classic OBSE commands past release 6, registered at their classic opcodes (from
// ClassicCommandTable.inl) so scripts compiled against classic OBSE call the right
// command. Commands that haven't been ported get a stub that reports itself and fails
// the script line, the same way the unknown opcode failed before.

// The game's controls (classic OSInputGlobals, class Controls here) are at +0x30 of the OS
// globals. Unreal sets each control's state in it, and QueryControlState compares (1 << query)
// with that: query 0 held (state 1), 1 just pressed (state 2), 2 just released (state 4).
// Classic's DirectInput path is still in it, for when Unreal isn't the source.
static RelocPtr <u8 *> g_osGlobals(0x0941F740);

typedef u32 (* _Controls_QueryControlState)(void * controls, u32 control, u32 query);
static RelocAddr <_Controls_QueryControlState> Controls_QueryControlState(0x0673EA30);

enum
{
	kControl_Count =	29,	// the mappable controls, Forward (0) to Grab (0x1C)

	kKeyQuery_Held =	0,
	kKeyQuery_Pressed =	1,
};

bool IsControlDown(u32 control)
{
	if(control >= kControl_Count) return false;

	// one Unreal handles itself
	bool down = false;
	if(PlayerControls_GetDown(control, &down)) return down;

	u8 * osGlobals = *g_osGlobals;
	void * controls = osGlobals ? *(void **)(osGlobals + 0x30) : nullptr;
	if(!controls) return false;

	return Controls_QueryControlState(controls, control, kKeyQuery_Held) ||
		Controls_QueryControlState(controls, control, kKeyQuery_Pressed);
}

static bool Cmd_IsControlPressed_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(IsControlDown(control))
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsControlPressed %d >> %d", control, (int)*result);

	return true;
}

// 1 the first time each script asks after the control went down, then 0 until it's released
// and pressed again, as in classic
static bool Cmd_OnControlDown_Execute(COMMAND_ARGS)
{
	static std::map <u32, std::set <u32>> s_reported;	// script form ID -> controls it has been told are down

	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control) || !script) return true;

	// Script derives from TESForm
	auto & reported = s_reported[reinterpret_cast <TESForm *>(script)->refID];

	if(IsControlDown(control))
	{
		if(reported.insert(control).second)
			*result = 1;
	}
	else
		reported.erase(control);

	return true;
}

// in Commands_Inventory.cpp
extern CommandInfo kCommandInfo_GetBaseObject;
extern CommandInfo kCommandInfo_GetObjectType;
extern CommandInfo kCommandInfo_IsWeapon;
extern CommandInfo kCommandInfo_IsAmmo;
extern CommandInfo kCommandInfo_IsArmor;
extern CommandInfo kCommandInfo_IsClothing;
extern CommandInfo kCommandInfo_IsBook;
extern CommandInfo kCommandInfo_IsIngredient;
extern CommandInfo kCommandInfo_IsContainer;
extern CommandInfo kCommandInfo_IsKey;
extern CommandInfo kCommandInfo_IsAlchemyItem;
extern CommandInfo kCommandInfo_IsApparatus;
extern CommandInfo kCommandInfo_IsSoulGem;
extern CommandInfo kCommandInfo_IsSigilStone;
extern CommandInfo kCommandInfo_IsDoor;
extern CommandInfo kCommandInfo_IsActivator;
extern CommandInfo kCommandInfo_IsLight;
extern CommandInfo kCommandInfo_IsMiscItem;
extern CommandInfo kCommandInfo_IsFurniture;
extern CommandInfo kCommandInfo_IsFlora;
extern CommandInfo kCommandInfo_GetGoldValue;
extern CommandInfo kCommandInfo_IsQuestItem;
extern CommandInfo kCommandInfo_SetQuestItem;
extern CommandInfo kCommandInfo_CompareModelPath;
extern CommandInfo kCommandInfo_CompareMaleGroundPath;
extern CommandInfo kCommandInfo_CopyModelPath;
extern CommandInfo kCommandInfo_CopyMaleGroundPath;
extern CommandInfo kCommandInfo_AddItemNS;
extern CommandInfo kCommandInfo_RemoveItemNS;
extern CommandInfo kCommandInfo_EquipItemNS;
extern CommandInfo kCommandInfo_UnequipItemNS;
extern CommandInfo kCommandInfo_GetObjectCharge;
extern CommandInfo kCommandInfo_GetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_ModEquippedCurrentCharge;
extern CommandInfo kCommandInfo_GetCurrentCharge;
extern CommandInfo kCommandInfo_SetCurrentCharge;
extern CommandInfo kCommandInfo_ModCurrentCharge;
extern CommandInfo kCommandInfo_SetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_GetObjectHealth;
extern CommandInfo kCommandInfo_GetEquippedCurrentHealth;
extern CommandInfo kCommandInfo_SetEquippedCurrentHealth;
extern CommandInfo kCommandInfo_ModEquippedCurrentHealth;
extern CommandInfo kCommandInfo_GetCurrentHealth;
extern CommandInfo kCommandInfo_SetCurrentHealth;
extern CommandInfo kCommandInfo_GetEquippedWeaponPoison;
extern CommandInfo kCommandInfo_SetEquippedWeaponPoison;
extern CommandInfo kCommandInfo_RemoveEquippedWeaponPoison;
extern CommandInfo kCommandInfo_GetHotKeyItem;
extern CommandInfo kCommandInfo_ClearHotKey;
extern CommandInfo kCommandInfo_SetHotKeyItem;

// in Commands_Actor.cpp
extern CommandInfo kCommandInfo_IsRefEssential;
extern CommandInfo kCommandInfo_SetRefEssential;
extern CommandInfo kCommandInfo_HasSpell;

// in Commands_Creature.cpp
extern CommandInfo kCommandInfo_IsCreature;
extern CommandInfo kCommandInfo_GetRider;

// in Commands_Menu.cpp
extern CommandInfo kCommandInfo_GetActiveMenuSelection;

// in Commands_MiscReference.cpp
extern CommandInfo kCommandInfo_GetOwner;
extern CommandInfo kCommandInfo_GetParentCellOwner;
extern CommandInfo kCommandInfo_GetOwningFactionRequiredRank;
extern CommandInfo kCommandInfo_GetParentCellOwningFactionRequiredRank;
extern CommandInfo kCommandInfo_IsOffLimits;
extern CommandInfo kCommandInfo_GetFirstRef;
extern CommandInfo kCommandInfo_GetNextRef;

// in Commands_Game.cpp
extern CommandInfo kCommandInfo_GetCrosshairRef;
extern CommandInfo kCommandInfo_GetGameLoaded;
extern CommandInfo kCommandInfo_GetGameRestarted;
extern CommandInfo kCommandInfo_MessageEX;
extern CommandInfo kCommandInfo_MessageBoxEX;

// in Commands_Loops.cpp, with the loops they keep
extern CommandInfo kCommandInfo_ForEach;
extern CommandInfo kCommandInfo_Loop;
extern CommandInfo kCommandInfo_Break;
extern CommandInfo kCommandInfo_Continue;

// in Commands_String.cpp, with the strings they keep
extern CommandInfo kCommandInfo_sv_Construct;
extern CommandInfo kCommandInfo_sv_Set;
extern CommandInfo kCommandInfo_sv_Length;
extern CommandInfo kCommandInfo_sv_SubString;
extern CommandInfo kCommandInfo_sv_Destruct;

// in Commands_Events.cpp, with the handlers they keep
extern CommandInfo kCommandInfo_SetEventHandler;
extern CommandInfo kCommandInfo_RemoveEventHandler;

// in Commands_Input.cpp, with OBSE64's own key commands
extern CommandInfo kCommandInfo_GetNumKeysPressed;
extern CommandInfo kCommandInfo_GetKeyPress;

// in FunctionScripts.cpp, with the calls they make
extern CommandInfo kCommandInfo_Call;
extern CommandInfo kCommandInfo_SetFunctionValue;

static CommandInfo kCommandInfo_IsControlPressed =
{
	"IsControlPressed", "",
	0,
	"returns true if the key or button assigned to control is pressed",
	0,
	1, kParams_OneInt,
	Cmd_IsControlPressed_Execute
};

static CommandInfo kCommandInfo_OnControlDown =
{
	"OnControlDown", "",
	0,
	"returns true each time the key or button assigned to control is depressed",
	0,
	1, kParams_OneInt,
	Cmd_OnControlDown_Execute
};

// Classic disabled and tapped the key a control was bound to. The Remastered's bindings are
// Unreal's, so these act on the control itself, where it's one Unreal handles on its own
// (PlayerControls.cpp): movement, Run (sprinting) and Always Run (the walk toggle), which is the
// only one that can be tapped. For any other, they do nothing, and say so once.
static void ReportControlCommand(const char * command, u32 control)
{
	static std::set <std::string> s_reported;

	char key[64];
	sprintf_s(key, "%s %u", command, control);

	if(s_reported.insert(key).second)
		_MESSAGE("%s %u: not supported for that control yet", command, control);
}

static bool Cmd_DisableControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(!PlayerControls_SetDisabled(control, true))
		ReportControlCommand("DisableControl", control);

	return true;
}

static bool Cmd_EnableControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(!PlayerControls_SetDisabled(control, false))
		ReportControlCommand("EnableControl", control);

	return true;
}

// 1 if the control was tapped
static bool Cmd_TapControl_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 control = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &control)) return true;

	if(PlayerControls_Tap(control))
		*result = 1;
	else
		ReportControlCommand("TapControl", control);

	return true;
}

static CommandInfo kCommandInfo_DisableControl =
{
	"DisableControl", "",
	0,
	"disables the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_DisableControl_Execute
};

static CommandInfo kCommandInfo_EnableControl =
{
	"EnableControl", "",
	0,
	"enables the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_EnableControl_Execute
};

static CommandInfo kCommandInfo_TapControl =
{
	"TapControl", "",
	0,
	"taps the key associated with the specified control",
	0,
	1, kParams_OneInt,
	Cmd_TapControl_Execute
};

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

// the runner Run's callers use, unless it's busy with another script (0x0694D580)
static RelocPtr <ScriptRunner *> g_scriptRunner(0x094904E8);

// The runners running a script on this thread, innermost last. Run's caller (0x0694D580) gives it
// the main runner, or a new one when that's busy, as for a function a script calls or a script
// that an Activate starts, and both calls (0x0694D6A8 and 0x0694D645) go through a hook that notes
// the runner while it runs. Run's last five arguments, on the stack, are passed on as they are.
typedef u64 (* _ScriptRunner_Run)(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9);
static RelocAddr <_ScriptRunner_Run> ScriptRunner_Run(0x0694D6D0);
static RelocAddr <uintptr_t> kRun_MainRunnerCall(0x0694D6A8);
static RelocAddr <uintptr_t> kRun_NewRunnerCall(0x0694D645);

static thread_local std::vector <ScriptRunner *> s_runners;
static bool s_runnersNoted = false;

static u64 ScriptRunner_Run_Hook(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9)
{
	s_runners.push_back(runner);
	u64 result = ScriptRunner_Run(runner, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	s_runners.pop_back();

	return result;
}

static void InstallRunnerHooks()
{
	uintptr_t run = ScriptRunner_Run.getUIntPtr();
	if(!IsCallTo(kRun_MainRunnerCall.getUIntPtr(), run) || !IsCallTo(kRun_NewRunnerCall.getUIntPtr(), run))
	{
		_ERROR("the script runner's calls aren't where expected; Goto and loops will work only in scripts the main runner runs");
		return;
	}

	g_branchTrampoline.write5Call(kRun_MainRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	g_branchTrampoline.write5Call(kRun_NewRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	s_runnersNoted = true;
}

// Run moves to the next statement by adding the statement's length to its arguments' offset. It
// passes the length to 0x0694C810 by pointer, as the fifth argument, which a command can change to
// jump, as classic's Goto did. 0x0694C810 calls commands from 0x0694D179 with opcodeOffsetPtr at its
// rsp + 0x40, and its fifth argument is at rsp + 0x800 (eight pushes, 0x798 of locals, the return
// address and four home slots).
static RelocAddr <uintptr_t> kStatementCommandReturn(0x0694D17C);
static const u32 kNextStatementLengthOffset = 0x800 - 0x40;

// where Label was, and the nesting there, by script and label
struct SavedIP
{
	u32	ip;
	s32	ifDepth;
	u32	ifStack[10];
};

static std::map <std::pair <Script *, u32>, SavedIP> s_savedIPs;

bool IsStatementCall(void * returnAddress)
{
	return uintptr_t(returnAddress) == kStatementCommandReturn.getUIntPtr();
}

bool GetStatementState(void * returnAddress, Script * script, ScriptLocals * locals, const char * scriptData,
	u32 argsOffset, u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length)
{
	if(!IsStatementCall(returnAddress)) return false;

	// The innermost runner, or without the hooks the main one. The main runner can be running this
	// script while another runner runs it too, inside that run (a function calling itself, a
	// script activating another reference with the same script), and putting its nesting back
	// there would leave the inner runner's to grow past its ten levels: so the locals must match.
	if(s_runnersNoted)
		*runner = s_runners.empty() ? nullptr : s_runners.back();
	else
		*runner = *g_scriptRunner;

	if(!*runner || (*runner)->script != script || (*runner)->locals != locals) return false;

	// the length Run read, which is just before the arguments
	*length = *(s32 **)((u8 *)opcodeOffsetPtr + kNextStatementLengthOffset);
	return *length && **length == *(const s16 *)(scriptData + argsOffset - 2);
}

static void ReportUnfollowedJump(const char * name, Script * script)
{
	static bool s_reported = false;
	if(!s_reported)
	{
		s_reported = true;
		_MESSAGE("%s in script %08X ignored: it only works as a statement of its own",
			name, script ? reinterpret_cast <TESForm *>(script)->refID : 0);
	}
}

static bool Cmd_SaveIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Label", script);
		return true;
	}

	SavedIP & saved = s_savedIPs[{ script, label }];
	saved.ip = argsOffset + *length;
	saved.ifDepth = runner->ifDepth;
	memcpy(saved.ifStack, runner->ifStack, sizeof(saved.ifStack));

	return true;
}

static bool Cmd_RestoreIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Goto", script);
		return true;
	}

	// classic jumped to the start of the script for a label it hadn't seen; do nothing instead
	auto saved = s_savedIPs.find({ script, label });
	if(saved == s_savedIPs.end()) return true;

	*length = s32(saved->second.ip) - s32(argsOffset);
	runner->ifDepth = saved->second.ifDepth;
	memcpy(runner->ifStack, saved->second.ifStack, sizeof(runner->ifStack));

	return true;
}

bool Cmd_Expression_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf)
{
	Console_Print("xOBSE64 can't compile OBSE expressions yet, so this command only works in compiled scripts");
	return false;
}

static ParamInfo kParams_OneInt_OneOptionalInt[2] =
{
	{	"int", kParamType_Integer, 0 },
	{	"int", kParamType_Integer, 1 },
};

static CommandInfo kCommandInfo_SetDebugMode =
{
	"SetDebugMode", "",
	0,
	"toggles debug mode for all scripts belonging to a mod",
	0,
	2, kParams_OneInt_OneOptionalInt,
	Cmd_SetDebugMode_Execute
};

static CommandInfo kCommandInfo_DebugPrint =
{
	"DebugPrint", "",
	0,
	"prints a formatted string to the console if debug mode is enabled for the calling script",
	0,
	21, kParams_FormatString,
	Cmd_DebugPrint_Execute
};

static CommandInfo kCommandInfo_PrintD =
{
	"PrintD", "",
	0,
	"prints a string expression to the console if debug mode is enabled for the calling script",
	0,
	0, nullptr,
	Cmd_PrintD_Execute,
	Cmd_Expression_Parse
};

static CommandInfo kCommandInfo_TestExpr =
{
	"TestExpr", "",
	0,
	"returns 1 if the expression evaluates without an error",
	0,
	0, nullptr,
	Cmd_TestExpr_Execute,
	Cmd_Expression_Parse
};

static ParamInfo kParams_OneString[1] =
{
	{	"string", kParamType_String, 0 },
};

static CommandInfo kCommandInfo_FileExists =
{
	"FileExists", "",
	0,
	"returns true if the specified file exists",
	0,
	1, kParams_OneString,
	Cmd_FileExists_Execute
};

static CommandInfo kCommandInfo_RunBatchScript =
{
	"RunBatchScript", "",
	0,
	"Similar to the 'bat' console command",
	0,
	3, kParams_RunBatchScript,
	Cmd_RunBatchScript_Execute
};

static CommandInfo kCommandInfo_SaveIP =
{
	"SaveIP", "",
	0,
	"marks a place in the script for RestoreIP (Goto) to jump back to",
	0,
	1, kParams_OneOptionalInt,
	Cmd_SaveIP_Execute
};

static CommandInfo kCommandInfo_RestoreIP =
{
	"RestoreIP", "",
	0,
	"jumps back to the place SaveIP (Label) marked",
	0,
	1, kParams_OneOptionalInt,
	Cmd_RestoreIP_Execute
};

struct PortedCommand
{
	const CommandInfo	* info;
	CommandReturnType	retnType;
};

// matched to their classic slot by name; the slot supplies the classic name and alias
static const PortedCommand kPortedCommands[] =
{
	{ &kCommandInfo_GetBaseObject,	kRetnType_Form },
	{ &kCommandInfo_GetCrosshairRef,	kRetnType_Form },
	{ &kCommandInfo_IsControlPressed,	kRetnType_Default },
	{ &kCommandInfo_OnControlDown,	kRetnType_Default },
	{ &kCommandInfo_GetOwner,		kRetnType_Form },
	{ &kCommandInfo_GetParentCellOwner,	kRetnType_Form },
	{ &kCommandInfo_GetOwningFactionRequiredRank,	kRetnType_Default },
	{ &kCommandInfo_GetParentCellOwningFactionRequiredRank,	kRetnType_Default },
	{ &kCommandInfo_IsOffLimits,	kRetnType_Default },
	{ &kCommandInfo_GetGoldValue,	kRetnType_Default },
	{ &kCommandInfo_IsRefEssential,	kRetnType_Default },
	{ &kCommandInfo_SetRefEssential,	kRetnType_Default },
	{ &kCommandInfo_GetObjectCharge,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_ModEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_IsQuestItem,	kRetnType_Default },
	{ &kCommandInfo_SetQuestItem,	kRetnType_Default },
	{ &kCommandInfo_CompareModelPath,	kRetnType_Default },
	{ &kCommandInfo_CompareMaleGroundPath,	kRetnType_Default },
	{ &kCommandInfo_CopyModelPath,	kRetnType_Default },
	{ &kCommandInfo_CopyMaleGroundPath,	kRetnType_Default },
	{ &kCommandInfo_SaveIP,			kRetnType_Default },
	{ &kCommandInfo_RestoreIP,		kRetnType_Default },
	{ &kCommandInfo_GetActiveMenuSelection,	kRetnType_Form },
	{ &kCommandInfo_GetGameLoaded,	kRetnType_Default },
	{ &kCommandInfo_AddItemNS,		kRetnType_Default },
	{ &kCommandInfo_GetFirstRef,	kRetnType_Form },
	{ &kCommandInfo_GetNextRef,		kRetnType_Form },
	{ &kCommandInfo_GetObjectType,	kRetnType_Default },
	{ &kCommandInfo_IsWeapon,		kRetnType_Default },
	{ &kCommandInfo_IsAmmo,			kRetnType_Default },
	{ &kCommandInfo_IsArmor,		kRetnType_Default },
	{ &kCommandInfo_IsClothing,		kRetnType_Default },
	{ &kCommandInfo_IsBook,			kRetnType_Default },
	{ &kCommandInfo_IsIngredient,	kRetnType_Default },
	{ &kCommandInfo_IsContainer,	kRetnType_Default },
	{ &kCommandInfo_IsKey,			kRetnType_Default },
	{ &kCommandInfo_IsAlchemyItem,	kRetnType_Default },
	{ &kCommandInfo_IsApparatus,	kRetnType_Default },
	{ &kCommandInfo_IsSoulGem,		kRetnType_Default },
	{ &kCommandInfo_IsSigilStone,	kRetnType_Default },
	{ &kCommandInfo_IsDoor,			kRetnType_Default },
	{ &kCommandInfo_IsActivator,	kRetnType_Default },
	{ &kCommandInfo_IsLight,		kRetnType_Default },
	{ &kCommandInfo_IsMiscItem,		kRetnType_Default },
	{ &kCommandInfo_IsFurniture,	kRetnType_Default },
	{ &kCommandInfo_IsFlora,		kRetnType_Default },
	{ &kCommandInfo_IsCreature,		kRetnType_Default },
	{ &kCommandInfo_SetDebugMode,	kRetnType_Default },
	{ &kCommandInfo_DebugPrint,		kRetnType_Default },
	{ &kCommandInfo_PrintD,			kRetnType_Default },
	{ &kCommandInfo_TestExpr,		kRetnType_Default },
	{ &kCommandInfo_MessageEX,		kRetnType_Default },
	{ &kCommandInfo_MessageBoxEX,	kRetnType_Default },
	{ &kCommandInfo_RunBatchScript,	kRetnType_Default },
	{ &kCommandInfo_sv_Construct,	kRetnType_String },
	{ &kCommandInfo_sv_Set,			kRetnType_Default },
	{ &kCommandInfo_sv_Length,		kRetnType_Default },
	{ &kCommandInfo_sv_SubString,	kRetnType_String },
	{ &kCommandInfo_sv_Destruct,	kRetnType_Default },
	{ &kCommandInfo_GetRider,		kRetnType_Form },
	{ &kCommandInfo_GetGameRestarted,	kRetnType_Default },
	{ &kCommandInfo_FileExists,		kRetnType_Default },
	{ &kCommandInfo_EquipItemNS,	kRetnType_Default },
	{ &kCommandInfo_UnequipItemNS,	kRetnType_Default },
	{ &kCommandInfo_SetEventHandler,	kRetnType_Default },
	{ &kCommandInfo_RemoveEventHandler,	kRetnType_Default },
	{ &kCommandInfo_DisableControl,	kRetnType_Default },
	{ &kCommandInfo_EnableControl,	kRetnType_Default },
	{ &kCommandInfo_TapControl,		kRetnType_Default },
	{ &kCommandInfo_GetNumKeysPressed,	kRetnType_Default },
	{ &kCommandInfo_GetKeyPress,	kRetnType_Default },
	{ &kCommandInfo_Call,			kRetnType_Ambiguous },
	{ &kCommandInfo_SetFunctionValue,	kRetnType_Default },
	{ &kCommandInfo_ForEach,		kRetnType_Default },
	{ &kCommandInfo_Loop,			kRetnType_Default },
	{ &kCommandInfo_Break,			kRetnType_Default },
	{ &kCommandInfo_Continue,		kRetnType_Default },
	{ &kCommandInfo_GetCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_SetCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_ModCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_RemoveItemNS,	kRetnType_Default },
	{ &kCommandInfo_HasSpell,		kRetnType_Default },
	{ &kCommandInfo_SetEquippedCurrentCharge,	kRetnType_Default },
	{ &kCommandInfo_GetObjectHealth,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_SetEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_ModEquippedCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_GetCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_SetCurrentHealth,	kRetnType_Default },
	{ &kCommandInfo_GetEquippedWeaponPoison,	kRetnType_Form },
	{ &kCommandInfo_SetEquippedWeaponPoison,	kRetnType_Default },
	{ &kCommandInfo_RemoveEquippedWeaponPoison,	kRetnType_Form },
	{ &kCommandInfo_GetHotKeyItem,	kRetnType_Form },
	{ &kCommandInfo_ClearHotKey,	kRetnType_Default },
	{ &kCommandInfo_SetHotKeyItem,	kRetnType_Default },
};

static bool Cmd_ClassicStub_Execute(COMMAND_ARGS)
{
	static std::set <u32> s_reported;

	// the opcode is the u16 four bytes before this command's parameters
	u32 opcode = (scriptData && *opcodeOffsetPtr >= 4) ? *(const u16 *)(scriptData + *opcodeOffsetPtr - 4) : 0;

	if(s_reported.insert(opcode).second)
	{
		CommandInfo * cmd = GetCommandInfo(opcode);
		const char * name = (cmd && cmd->name && *cmd->name) ? cmd->name : "<removed classic command>";

		// Script derives from TESForm
		u32 scriptID = script ? reinterpret_cast <TESForm *>(script)->refID : 0;

		_MESSAGE("classic command %s (%04X) called by script %08X is not implemented yet", name, opcode, scriptID);

		// shown however the script runs (Console_Print drops lines outside a typed command), and
		// logged above already
		char line[256];
		snprintf(line, sizeof(line), "xOBSE64: %s is not implemented yet (script %08X)", name, scriptID);
		ScriptConsolePrint(line, false);
	}

	return false;
}

static const CommandInfo kClassicStubCommand =
{
	"", "",
	0,
	"classic OBSE command that is not implemented in xOBSE64 yet",
	0,
	0, nullptr,
	Cmd_ClassicStub_Execute
};

// The console command a classic slot named con_ and its name or short name stands for: classic
// OBSE copied some console commands into the script commands so scripts could run them, naming
// each copy so (con_TCL). A copy mustn't take the console command's own name: the game looks a
// name up in the script commands before the console's (0x06948B20), so the console would run the
// copy, or the stub in its place.
static const CommandInfo * FindImportedConsoleCommand(const char * slotName)
{
	if(_strnicmp(slotName, "con_", 4)) return nullptr;

	const char * name = slotName + 4;
	for(u32 i = 0; i < kScript_NumConsoleCommands; i++)
	{
		const CommandInfo & cmd = g_firstConsoleCommand[i];
		if((cmd.name && !_stricmp(name, cmd.name)) || (cmd.shortName && *cmd.shortName && !_stricmp(name, cmd.shortName)))
			return &cmd;
	}

	return nullptr;
}

struct ClassicSlot
{
	u32			opcode;
	const char	* name;
	const char	* shortName;
};

static const ClassicSlot kClassicSlots[] =
{
#define CLASSIC(opcode, name, shortName) { opcode, name, shortName },
#include "ClassicCommandTable.inl"
#undef CLASSIC
};

void AddClassicCommands()
{
	const auto & first = kClassicSlots[0];
	const auto & last = kClassicSlots[sizeof(kClassicSlots) / sizeof(kClassicSlots[0]) - 1];

	if(GetNextScriptOpcode() != first.opcode)
	{
		_ERROR("classic commands start at opcode %04X but the command table is at %04X, not adding them",
			first.opcode, GetNextScriptOpcode());
		return;
	}

	// vanilla commands that return a form, as classic OBSE records them: PlaceAtMe, GetActionRef,
	// GetSelf, GetContainer, GetCombatTarget, GetPackageTarget, GetParentRef, CreateFullActorCopy
	static const u32 kVanillaFormCommands[] = { 0x1025, 0x10CD, 0x10CE, 0x10CF, 0x10E8, 0x10E9, 0x1113, 0x1167 };
	for(u32 opcode : kVanillaFormCommands)
		SetCommandReturnType(opcode, kRetnType_Form);

	u32 numPorted = 0;

	for(const auto & slot : kClassicSlots)
	{
		CommandInfo info = kClassicStubCommand;
		CommandReturnType retnType = kRetnType_Default;

		if(const CommandInfo * console = FindImportedConsoleCommand(slot.name))
		{
			info = *console;
			numPorted++;
		}
		else
		{
			for(const auto & ported : kPortedCommands)
			{
				if(*slot.name && !_stricmp(ported.info->name, slot.name))
				{
					info = *ported.info;
					retnType = ported.retnType;
					numPorted++;
					break;
				}
			}
		}

		info.name = slot.name;
		info.shortName = slot.shortName;

		AddScriptCommand(info, retnType);
	}

	_MESSAGE("classic commands %04X-%04X added, %d ported", first.opcode, last.opcode, numPorted);

	InstallMagicMenuHooks();
	InstallGameLoadedHooks();
	InstallItemMessageHook();
	InstallStringVarHooks();
	InstallUserFunctionBlock();
	PlayerControls_Install();
	InstallIsRunning();
	InstallRunnerHooks();
}
