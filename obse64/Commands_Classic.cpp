#include "CommandTable.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// Classic OBSE commands past release 6, registered at their classic opcodes (from
// ClassicCommandTable.inl) so scripts compiled against classic OBSE call the right
// command. Commands that haven't been ported get a stub that reports itself and fails
// the script line, the same way the unknown opcode failed before.

CommandInfo * GetCommandInfo(u32 opcode);	// Hooks_Script.cpp

// a reference passed as the argument stands for its base object, as in classic OBSE
static TESForm * FormArgOrBase(TESForm * form, TESObjectREFR * thisObj)
{
	if(form)
	{
		if(auto * refr = DYNAMIC_CAST(form, TESForm, TESObjectREFR))
			return refr->baseForm;

		return form;
	}

	return thisObj ? thisObj->baseForm : nullptr;
}

static bool Cmd_GetBaseObject_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj || !thisObj->baseForm) return true;

	SetFormResult(result, thisObj->baseForm->refID);

	if(IsConsoleMode())
		Console_Print("GetBaseObject >> %08X", thisObj->baseForm->refID);

	return true;
}

static bool Cmd_GetObjectType_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
		*result = form->typeID;

	if(IsConsoleMode())
		Console_Print("GetObjectType >> %d", (int)*result);

	return true;
}

static bool IsFormType_Execute(COMMAND_ARGS, u8 typeID, const char * name)
{
	*result = 0;

	TESForm * form = nullptr;
	ExtractArgs(EXTRACT_ARGS, &form);

	form = FormArgOrBase(form, thisObj);
	if(form && form->typeID == typeID)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("%s >> %d", name, (int)*result);

	return true;
}

static CommandInfo kCommandInfo_GetBaseObject =
{
	"GetBaseObject", "",
	0,
	"returns the base object of the reference",
	1,
	0, nullptr,
	Cmd_GetBaseObject_Execute
};

static CommandInfo kCommandInfo_GetObjectType =
{
	"GetObjectType", "",
	0,
	"returns the type id of the specified object or the reference on which it was called",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetObjectType_Execute
};

#define IS_FORM_TYPE(name, type, what)													\
	static bool Cmd_##name##_Execute(COMMAND_ARGS)										\
	{																					\
		return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_##type, #name);			\
	}																					\
																						\
	static CommandInfo kCommandInfo_##name =											\
	{																					\
		#name, "",																		\
		0,																				\
		"returns 1 if the specified base object is " what " and 0 if it is not",		\
		0,																				\
		1, kParams_OneOptionalInventoryObject,											\
		Cmd_##name##_Execute															\
	};

IS_FORM_TYPE(IsWeapon,		Weapon,			"a weapon")
IS_FORM_TYPE(IsAmmo,		Ammo,			"ammo")
IS_FORM_TYPE(IsArmor,		Armor,			"armor")
IS_FORM_TYPE(IsClothing,	Clothing,		"clothing")
IS_FORM_TYPE(IsBook,		Book,			"a book")
IS_FORM_TYPE(IsIngredient,	Ingredient,		"an ingredient")
IS_FORM_TYPE(IsContainer,	Container,		"a container")
IS_FORM_TYPE(IsKey,			Key,			"a key")
IS_FORM_TYPE(IsAlchemyItem,	AlchemyItem,	"an alchemy item")
IS_FORM_TYPE(IsApparatus,	Apparatus,		"an apparatus")
IS_FORM_TYPE(IsSoulGem,		SoulGem,		"a soul gem")
IS_FORM_TYPE(IsSigilStone,	SigilStone,		"a sigil stone")
IS_FORM_TYPE(IsDoor,		Door,			"a door")
IS_FORM_TYPE(IsActivator,	Activator,		"an activator")
IS_FORM_TYPE(IsLight,		Light,			"a light")
IS_FORM_TYPE(IsMiscItem,	Misc,			"a misc item")
IS_FORM_TYPE(IsFurniture,	Furniture,		"furniture")
IS_FORM_TYPE(IsFlora,		Flora,			"flora")

#undef IS_FORM_TYPE

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

// Expands a classic OBSE format string. OBSE's own specifiers are replaced in place, and any
// other % takes the next argument as a number, as classic OBSE left those to sprintf. The ones
// that need a form, a string or a name table (%n %i %c %p %k %v %z) still use up their
// arguments, so later numbers line up, but print "?" for now: the game hands the arguments
// over as floats, which can't carry a form ID. False if there are too few arguments.
static bool FormatClassicString(const char * fmt, const float * args, u32 numArgs, std::string * out)
{
	u32 argIdx = 0;
	auto nextArg = [&](double * value)
	{
		if(argIdx >= numArgs) return false;
		*value = args[argIdx++];
		return true;
	};

	out->clear();

	for(const char * p = fmt; *p; p++)
	{
		if(*p != '%')
		{
			*out += *p;
			continue;
		}

		double value = 0;

		switch(p[1])
		{
			case '%':	*out += '%';	p++;	break;
			case 'r':
			case 'R':	*out += '\n';	p++;	break;
			case 'q':
			case 'Q':	*out += '"';	p++;	break;
			case 'e':
			case 'E':					p++;	break;	// empty: works around the CS rejecting empty strings
			case 'B':	*out += '\2';	p++;	break;	// blue console text on
			case 'b':	*out += '\3';	p++;	break;	// and off
			case '}':					p++;	break;

			case 'a':
			case 'A':
				if(!nextArg(&value)) return false;
				*out += char(int(value));
				p++;
				break;

			case 'n': case 'N':
			case 'i': case 'I':
			case 'k': case 'K':
			case 'v': case 'V':
			case 'z': case 'Z':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case 'p':
			case 'P':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				if(p[1]) p++;	// the o, s or p that picks the pronoun
				break;

			case 'c':
			case 'C':
				if(!nextArg(&value) || !nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case '{':
				// %{text%} keeps text only if the argument is nonzero
				if(!nextArg(&value)) return false;
				p++;
				if(value == 0)
				{
					const char * close = strstr(p + 1, "%}");
					p = close ? close + 1 : p + strlen(p) - 1;
				}
				break;

			case 'x':
			case 'X':
			{
				if(!nextArg(&value)) return false;
				p++;

				int width = 0;
				if(isdigit((u8)p[1]))	// a single-digit width may follow
					width = *++p - '0';

				char buf[32];
				snprintf(buf, sizeof(buf), "%0*llX", width, (unsigned long long)value);
				*out += buf;
			}
			break;

			default:
			{
				// a printf-style number: flags, width, precision, conversion
				const char * end = p + 1;
				while(*end && strchr("-+ #0", *end)) end++;
				while(isdigit((u8)*end)) end++;
				if(*end == '.')
				{
					end++;
					while(isdigit((u8)*end)) end++;
				}

				if(!nextArg(&value)) return false;

				char buf[128];
				if(*end && strchr("fFgGeE", *end))
				{
					snprintf(buf, sizeof(buf), std::string(p, end + 1).c_str(), value);
					*out += buf;
				}
				else if(*end && strchr("diu", *end))
				{
					std::string spec(p, end);
					spec += (*end == 'u') ? "llu" : "lld";
					snprintf(buf, sizeof(buf), spec.c_str(), (long long)value);
					*out += buf;
				}
				else
				{
					// not a number conversion: print it as written
					out->append(p, *end ? end + 1 : end);
				}

				p = *end ? end : end - 1;
			}
			break;
		}
	}

	return true;
}

static ParamInfo kParams_FormatString[21] =
{
	{	"format string",	kParamType_String,	0 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
};

// reads a command's format string and up to 20 numbers, as classic OBSE's PrintToConsole does
static bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out)
{
	// the argument count comes first, and includes the format string
	u16 numArgs = *(const u16 *)(scriptData + *opcodeOffsetPtr);

	char fmt[0x4000] = { 0 };
	float f[20] = { 0 };

	if(!ExtractArgs(EXTRACT_ARGS, fmt,
		&f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9],
		&f[10], &f[11], &f[12], &f[13], &f[14], &f[15], &f[16], &f[17], &f[18], &f[19]))
		return false;

	return FormatClassicString(fmt, f, numArgs ? numArgs - 1 : 0, out);
}

static bool Cmd_DebugPrint_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(IsDebugModeOn(script))
	{
		std::string text;
		if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text))
			Console_Print("%s", text.c_str());
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
			Console_Print("%s", eval.Arg(0).string.c_str());
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

// classic OBSE compiles these commands' arguments with its own expression compiler, which
// isn't ported: they run in mods' compiled scripts but can't be typed in the console yet
static bool Cmd_Expression_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf)
{
	Console_Print("OBSE64 can't compile OBSE expressions yet, so this command only works in compiled scripts");
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

struct PortedCommand
{
	const CommandInfo	* info;
	CommandReturnType	retnType;
};

// matched to their classic slot by name; the slot supplies the classic name and alias
static const PortedCommand kPortedCommands[] =
{
	{ &kCommandInfo_GetBaseObject,	kRetnType_Form },
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
	{ &kCommandInfo_SetDebugMode,	kRetnType_Default },
	{ &kCommandInfo_DebugPrint,		kRetnType_Default },
	{ &kCommandInfo_PrintD,			kRetnType_Default },
	{ &kCommandInfo_TestExpr,		kRetnType_Default },
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
		Console_Print("OBSE64: %s is not implemented yet (script %08X)", name, scriptID);
	}

	return false;
}

static const CommandInfo kClassicStubCommand =
{
	"", "",
	0,
	"classic OBSE command that is not implemented in OBSE64 yet",
	0,
	0, nullptr,
	Cmd_ClassicStub_Execute
};

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

		info.name = slot.name;
		info.shortName = slot.shortName;

		AddScriptCommand(info, retnType);
	}

	_MESSAGE("classic commands %04X-%04X added, %d ported", first.opcode, last.opcode, numPorted);
}
