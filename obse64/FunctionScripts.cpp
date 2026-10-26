#include "FunctionScripts.h"
#include "GameForms.h"
#include "obse64_common/Log.h"
#include "obse64_common/Relocation.h"
#include "obse64_common/SafeWrite.h"
#include <cmath>
#include <cstring>
#include <map>

// A script's locals (classic's ScriptEventList, 0x28 bytes). Its variables are a list at +0x18,
// each with its index and its value. The game builds a script's locals with 0x0662EB70, and takes
// them apart with 0x0662E5F0 before freeing the 0x28 bytes, as ScriptRunner::Run does with the
// ones it makes for a script run without any.
struct ScriptVar
{
	u32		idx;	// 00
	u32		pad04;	// 04
	double	value;	// 08
	u8		type;	// 10
};

struct ScriptVarNode
{
	ScriptVar		* var;
	ScriptVarNode	* next;
};

struct ScriptLocalsLayout
{
	Script			* script;	// 00
	u64				unk08;		// 08
	void			* events;	// 10
	ScriptVarNode	* vars;		// 18
	void			* effect;	// 20 a magic effect script's state
};

static_assert(sizeof(ScriptLocalsLayout) == 0x28);

typedef ScriptLocals * (* _Script_CreateLocals)(Script * script);
static RelocAddr <_Script_CreateLocals> Script_CreateLocals(0x0662EB70);

typedef void (* _ScriptLocals_Destruct)(ScriptLocals * locals);
static RelocAddr <_ScriptLocals_Destruct> ScriptLocals_Destruct(0x0662E5F0);

// the game's operator delete
typedef void (* _Heap_Free)(void * buf);
static RelocAddr <_Heap_Free> Heap_Free(0x00E26500);

// ScriptLocals_GetVariableValue keeps the locals it last searched and the variable it found there,
// and trusts them while the locals' address is the same, so it has to forget locals that are freed
static RelocPtr <ScriptLocals *> g_lastLocalsSearched(0x094800D8);

// Classic's Script::Execute: runs the script on these locals, in the game's ScriptRunner or a new
// one if that's busy. Unless forced, a script under 5 bytes isn't run.
typedef bool (* _Script_Execute)(Script * script, TESObjectREFR * thisObj, ScriptLocals * locals, TESObjectREFR * containingObj, bool force);
static RelocAddr <_Script_Execute> Script_Execute(0x06630EE0);

// The game keeps its script block types as commands too, a table of CommandInfo from GameMode (0).
// Begin calls the block type's handler and runs the block when it sets the result, and stops the
// script with an error when the type needs a calling reference and there is none. Type 7,
// OnYield_unused, never runs and needs one; classic OBSE made it Function.
static RelocPtr <CommandInfo> g_blockTypes(0x08FB95A0);

enum
{
	kScriptOffset_DataLength =	0x38,
	kScriptOffset_Data =		0x50,

	kStatement_Begin =		0x10,
	kBlockType_Function =	7,

	// classic Script::VariableType, as Begin Function gives a parameter's type
	kVarType_Float =	0,
	kVarType_Integer =	1,
	kVarType_Ref =		4,

	kMaxNestDepth =	30,	// classic's limit on functions calling functions
};

struct FunctionParam
{
	u16	varIdx;
	u8	varType;
};

struct UserFunction
{
	bool						isFunction = false;
	std::vector <FunctionParam>	params;
	ScriptLocals				* locals = nullptr;	// kept for the next call
	u32							running = 0;		// calls not yet returned
};

static std::map <Script *, UserFunction>	s_functions;
static std::vector <Script *>				s_callStack;	// function scripts being called, innermost last

// A function script's parameters, from its Begin Function. After ScriptName (opcode 0x1D, no data)
// comes Begin (0x10) and its length, the block type and the block's length, then classic OBSE's
// data: a version (0 or 1), the parameter count, and for each parameter its variable's index (u16)
// and type (u8). False if the script doesn't start with a Function block.
static bool ReadFunctionParams(Script * script, std::vector <FunctionParam> * out)
{
	const u8 * data = *(const u8 * const *)((const u8 *)script + kScriptOffset_Data);
	u32 dataLength = *(const u32 *)((const u8 *)script + kScriptOffset_DataLength);
	if(!data || dataLength < 16) return false;

	if(*(const u16 *)(data + 4) != kStatement_Begin || *(const u16 *)(data + 8) != kBlockType_Function)
		return false;

	const u8 * end = data + 8 + *(const u16 *)(data + 6);
	const u8 * p = data + 14;
	if(end > data + dataLength || p + 2 > end) return false;

	u8 version = *p++;
	if(version > 1) return false;

	u8 numParams = *p++;
	if(p + numParams * 3 > end) return false;

	for(u32 i = 0; i < numParams; i++, p += 3)
		out->push_back({ *(const u16 *)p, p[2] });

	return true;
}

static void SetVariable(ScriptLocals * locals, const FunctionParam & param, double value)
{
	for(auto * node = reinterpret_cast <ScriptLocalsLayout *>(locals)->vars; node; node = node->next)
	{
		if(!node->var || node->var->idx != param.varIdx) continue;

		switch(param.varType)
		{
			case kVarType_Integer:	node->var->value = floor(value);	break;
			case kVarType_Float:	node->var->value = value;			break;
			case kVarType_Ref:		node->var->value = value;			break;	// forms are kept as their form ID
		}

		return;
	}
}

// a new call starts with its variables at 0, as classic's did
static void ResetVariables(ScriptLocals * locals)
{
	for(auto * node = reinterpret_cast <ScriptLocalsLayout *>(locals)->vars; node; node = node->next)
	{
		if(node->var)
			node->var->value = 0;
	}
}

static void FreeLocals(ScriptLocals * locals)
{
	ScriptLocals_Destruct(locals);
	Heap_Free(locals);

	if(*g_lastLocalsSearched == locals)
		*g_lastLocalsSearched = nullptr;
}

bool CallUserFunction(Script * script, TESObjectREFR * thisObj, const std::vector <double> & args)
{
	if(!script) return false;

	// Script derives from TESForm
	u32 scriptID = reinterpret_cast <TESForm *>(script)->refID;

	auto iter = s_functions.find(script);
	if(iter == s_functions.end())
	{
		UserFunction fn;
		fn.isFunction = ReadFunctionParams(script, &fn.params);
		if(!fn.isFunction)
			_MESSAGE("script %08X was called as a function, but has no Function block", scriptID);

		iter = s_functions.emplace(script, fn).first;
	}

	UserFunction & fn = iter->second;
	if(!fn.isFunction) return false;

	if(s_callStack.size() >= kMaxNestDepth)
	{
		_MESSAGE("function script %08X not called: functions are nested %d deep", scriptID, kMaxNestDepth);
		return false;
	}

	// a function that's running already (it set off an event it handles) gets locals of its own
	bool temporary = fn.running != 0;

	ScriptLocals * locals = temporary ? nullptr : fn.locals;
	if(locals)
		ResetVariables(locals);
	else
	{
		locals = Script_CreateLocals(script);
		if(!locals) return false;

		if(!temporary)
			fn.locals = locals;
	}

	for(u32 i = 0; i < fn.params.size() && i < args.size(); i++)
		SetVariable(locals, fn.params[i], args[i]);

	fn.running++;
	s_callStack.push_back(script);

	bool ran = Script_Execute(script, thisObj, locals, nullptr, false);

	s_callStack.pop_back();
	fn.running--;

	if(temporary)
		FreeLocals(locals);

	return ran;
}

static bool Cmd_FunctionBlock_Execute(COMMAND_ARGS)
{
	*result = (!s_callStack.empty() && s_callStack.back() == script) ? 1 : 0;

	return true;
}

void InstallUserFunctionBlock()
{
	CommandInfo * info = g_blockTypes + kBlockType_Function;
	if(info->opcode != kBlockType_Function || !info->name || _stricmp(info->name, "OnYield_unused"))
	{
		_ERROR("block type %d isn't OnYield_unused; Function blocks won't run", kBlockType_Function);
		return;
	}

	safeWrite8(uintptr_t(&info->needsParent), 0);
	safeWrite64(uintptr_t(&info->execute), u64(&Cmd_FunctionBlock_Execute));
}
