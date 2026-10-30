#include "Commands_Classic.h"
#include "FormatString.h"
#include "CommandTable.h"
#include "FunctionScripts.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <ctime>
#include <intrin.h>
#include <map>
#include <string>

// Classic OBSE's string variables. A script's string_var is an integer variable holding a string's
// ID, and the strings themselves are kept here. Classic saved them with the game in its co-save,
// which OBSE64 doesn't have yet, so they last until the game closes. A variable can still hold an
// ID from a save made in an earlier session: setting that string brings it back under its ID, and
// new strings take IDs from a range picked when the game starts, so they don't land on such an ID.
class StringVarMap
{
public:
	std::string * Get(u32 id)
	{
		auto iter = m_strings.find(id);
		return (iter != m_strings.end()) ? &iter->second : nullptr;
	}

	// the string with this ID, empty if it was made in an earlier session
	std::string * GetOrBringBack(u32 id)
	{
		if(!id || id > kMaxID) return nullptr;

		return &m_strings[id];
	}

	u32 Add()
	{
		if(!m_nextID)
			m_nextID = 1 + u32((u64(time(nullptr)) * 2654435761u) % kIDRange);

		while(m_strings.count(m_nextID))
			m_nextID++;

		u32 id = m_nextID++;
		m_strings[id];

		return id;
	}

	void Remove(u32 id)
	{
		m_strings.erase(id);
	}

private:
	enum
	{
		kMaxID =	0x7FFFFFFF,	// string variables are integer variables
		kIDRange =	0x40000000,	// where a session's first ID can be, leaving room after it
	};

	std::map <u32, std::string>	m_strings;
	u32							m_nextID = 0;
};

static StringVarMap s_stringVars;

const std::string * LookupStringVar(u32 id)
{
	return s_stringVars.Get(id);
}

static u32 StringIDFromValue(double value)
{
	return u32(s64(value));
}

enum
{
	kStatement_Set =		0x15,
	kStatement_RefPrefix =	0x1C,	// the next statement runs on a reference; no length of its own

	kScriptOffset_DataLength =	0x38,
	kScriptOffset_Data =		0x50,
};

// The statement executor (0x0694C810) hands a set statement's expression to the evaluator
// (0x06A02190) from 0x0694CB4C, as a pointer into the script's bytecode. The evaluator copies the
// expression to its own stack before reading it, so a command inside it gets a pointer into that
// copy and can't see the statement around it, which classic found by reading the evaluator's
// stack. OBSE64 notes the expression on the way instead.
typedef double (* _Expression_Eval)(void * evaluator, const char * expr, TESObjectREFR * thisObj,
	TESObjectREFR * containingObj, Script * script, ScriptLocals * locals, u32 exprLength, u8 unk);
static RelocAddr <_Expression_Eval> Expression_Eval(0x06A02190);
static RelocAddr <uintptr_t> kSetStatement_EvalCall(0x0694CB4C);

static const char * s_setExpression = nullptr;	// the set statement's expression being evaluated
static Script * s_setScript = nullptr;

static double SetStatement_Eval_Hook(void * evaluator, const char * expr, TESObjectREFR * thisObj,
	TESObjectREFR * containingObj, Script * script, ScriptLocals * locals, u32 exprLength, u8 unk)
{
	const char * outerExpression = s_setExpression;
	Script * outerScript = s_setScript;

	s_setExpression = expr;
	s_setScript = script;

	double value = Expression_Eval(evaluator, expr, thisObj, containingObj, script, locals, exprLength, unk);

	s_setExpression = outerExpression;
	s_setScript = outerScript;

	return value;
}

void InstallStringVarHooks()
{
	if(!IsCallTo(kSetStatement_EvalCall.getUIntPtr(), Expression_Eval.getUIntPtr()))
	{
		_ERROR("the set statement's call to the evaluator isn't where expected; sv_Construct and sv_SubString won't assign");
		return;
	}

	g_branchTrampoline.write5Call(kSetStatement_EvalCall.getUIntPtr(), uintptr_t(SetStatement_Eval_Hook));
}

// The current value of the variable that the set statement being evaluated assigns to, read as the
// game reads a variable. Statements are an opcode, a length and that many bytes (apart from the
// reference prefix), so walking them from the start of the script finds the one holding the
// expression; a set statement's variable comes first, written as the game's arguments are.
static bool GetSetStatementVariable(TESObjectREFR * thisObj, TESObjectREFR * containingObj, Script * script,
	ScriptLocals * locals, double * value)
{
	if(!script || script != s_setScript || !s_setExpression) return false;

	const char * data = *(const char * const *)((const u8 *)script + kScriptOffset_Data);
	u32 dataLength = *(const u32 *)((const u8 *)script + kScriptOffset_DataLength);
	if(!data || s_setExpression < data || s_setExpression >= data + dataLength) return false;

	u32 exprOffset = u32(s_setExpression - data);

	u32 pos = 0;
	while(pos < exprOffset)
	{
		u16 opcode = *(const u16 *)(data + pos);
		if(opcode == kStatement_RefPrefix)
		{
			pos += 4;
			continue;
		}

		u32 end = pos + 4 + *(const u16 *)(data + pos + 2);
		if(exprOffset < end)
		{
			if(opcode != kStatement_Set) return false;

			u32 varOffset = pos + 4;
			return ExtractNumericArg(value, data, &varOffset, thisObj, containingObj, script, locals, true);
		}

		pos = end;
	}

	return false;
}

// Classic's commands that make a string are the value of a set statement: they put their text in
// the string the variable already holds, or in a new one, and return its ID for the variable.
static void AssignToStringVar(COMMAND_ARGS, const std::string & text, const char * name)
{
	double current = 0;
	if(!GetSetStatementVariable(thisObj, containingObj, script, locals, &current))
	{
		static bool s_reported = false;
		if(!s_reported)
		{
			s_reported = true;
			_MESSAGE("%s in script %08X ignored: it only works as the value of a set statement so far",
				name, script ? reinterpret_cast <TESForm *>(script)->refID : 0);
		}

		return;
	}

	u32 id = StringIDFromValue(current);
	std::string * str = s_stringVars.GetOrBringBack(id);
	if(!str)
	{
		id = s_stringVars.Add();
		str = s_stringVars.Get(id);
	}

	*str = text;
	*result = id;

	if(IsConsoleMode())
		Console_Print("%s >> \"%s\"", name, text.size() < 480 ? text.c_str() : "(too long to print)");
}

static bool Cmd_sv_Construct_Execute(COMMAND_ARGS)
{
	*result = 0;

	// as in classic, the string is made even if the arguments don't fit
	std::string text;
	ExtractClassicFormatString(PASS_COMMAND_ARGS, &text);

	AssignToStringVar(PASS_COMMAND_ARGS, text, "sv_Construct");

	return true;
}

// the string variable to set comes after the format string's numbers
static bool Cmd_sv_Set_Execute(COMMAND_ARGS)
{
	*result = 0;

	std::string text;
	std::vector <double> rest;
	if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text, &rest) && !rest.empty())
	{
		if(std::string * str = s_stringVars.GetOrBringBack(StringIDFromValue(rest[0])))
			*str = text;
	}

	return true;
}

static bool Cmd_sv_Length_Execute(COMMAND_ARGS)
{
	*result = -1;

	u32 id = 0;
	if(ExtractArgs(EXTRACT_ARGS, &id))
	{
		if(const std::string * str = s_stringVars.Get(id))
			*result = double(str->size());
	}

	if(IsConsoleMode())
		Console_Print("sv_Length >> %d", (int)*result);

	return true;
}

static bool Cmd_sv_SubString_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 id = 0;
	u32 start = 0;
	u32 count = 0xFFFFFFFF;	// to the end
	if(!ExtractArgs(EXTRACT_ARGS, &id, &start, &count)) return true;

	// classic assigned nothing when there was no string to take from
	const std::string * str = s_stringVars.Get(id);
	if(!str) return true;

	std::string sub = (start < str->size()) ? str->substr(start, count) : std::string();
	AssignToStringVar(PASS_COMMAND_ARGS, sub, "sv_SubString");

	return true;
}

// Frees strings, as classic's did: as the value of a set statement, the one its variable holds
// ("set s to sv_Destruct", which leaves s 0), and as a statement of its own, the ones the string
// variables it's given hold ("sv_Destruct s1 s2", up to ten), setting this script's own to 0.
// Classic's docs tell scripts to free a string they need only for a while this way, a function's
// above all: its variables are 0 again at each call, so each string it makes is a new one.
static bool Cmd_sv_Destruct_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();

	*result = 0;

	double current = 0;
	if(!IsStatementCall(returnAddress) && GetSetStatementVariable(thisObj, containingObj, script, locals, &current))
	{
		s_stringVars.Remove(StringIDFromValue(current));
		return true;
	}

	ObseExpression eval(PASS_COMMAND_ARGS);
	if(!eval.ExtractArgs()) return true;

	for(u32 i = 0; i < eval.NumArgs(); i++)
	{
		const auto & arg = eval.Arg(i);
		if(arg.varType != ObseExpression::kVarType_String) continue;

		s_stringVars.Remove(StringIDFromValue(arg.number));

		// a quest's or a reference's variable keeps its number, which no longer names a string
		if(arg.isLocalVar)
			SetScriptVariable(locals, arg.varIdx, 0);
	}

	return true;
}

static ParamInfo kParams_SubString[3] =
{
	{	"stringVar",	kParamType_Integer,	0 },
	{	"startPos",		kParamType_Integer,	1 },
	{	"howMany",		kParamType_Integer,	1 },
};

CommandInfo kCommandInfo_sv_Construct =
{
	"sv_Construct", "",
	0,
	"returns a formatted string",
	0,
	21, kParams_FormatString,
	Cmd_sv_Construct_Execute
};

CommandInfo kCommandInfo_sv_Set =
{
	"sv_Set", "",
	0,
	"sets the contents of a string variable",
	0,
	21, kParams_FormatString,
	Cmd_sv_Set_Execute
};

CommandInfo kCommandInfo_sv_Length =
{
	"sv_Length", "",
	0,
	"returns the number of characters in a string",
	0,
	1, kParams_OneInt,
	Cmd_sv_Length_Execute
};

CommandInfo kCommandInfo_sv_SubString =
{
	"sv_SubString", "",
	0,
	"returns a substring of a string variable",
	0,
	3, kParams_SubString,
	Cmd_sv_SubString_Execute
};

CommandInfo kCommandInfo_sv_Destruct =
{
	"sv_Destruct", "",
	0,
	"destroys one or more string variables",
	0,
	0, nullptr,
	Cmd_sv_Destruct_Execute,
	Cmd_Expression_Parse
};
