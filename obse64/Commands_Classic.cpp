#include "Commands_Classic.h"
#include "CommandTable.h"
#include "GameConsole.h"
#include "GameExtraData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <map>
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

// The interface manager keeps the reference the player would activate at +0x158. Unreal casts
// the player's line of sight and hands the hit over (0x0655A460); the setter (0x065D6950) stores
// it there while it's within reach and clears it otherwise, and the player's own activation reads
// it from there (0x065694B0). Classic read HUDInfoMenu::crosshairRef, which stays empty here.
static RelocPtr <u8 *> g_interfaceManager(0x0941F808);

static bool Cmd_GetCrosshairRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESObjectREFR * refr = nullptr;
	if(u8 * intfc = *g_interfaceManager)
	{
		if(auto * form = *(TESForm **)(intfc + 0x158))
			refr = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
	}

	if(refr)
		SetFormResult(result, refr->refID);

	if(IsConsoleMode())
		Console_Print("GetCrosshairRef >> %08X (%s)", refr ? refr->refID : 0, refr ? GetFullName(refr) : "nothing");

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

static CommandInfo kCommandInfo_GetCrosshairRef =
{
	"GetCrosshairRef", "",
	0,
	"returns the reference under the crosshair",
	0,
	0, nullptr,
	Cmd_GetCrosshairRef_Execute
};

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

static bool IsControlDown(u32 control)
{
	if(control >= kControl_Count) return false;

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

// Ownership is kept in a reference's extra data, or in its cell's for references (and their
// contents) that have none of their own. Vanilla SetOwnership and ClearOwnership change only the
// owner, so a required rank stays with the reference.
static TESForm * GetOwner(const BaseExtraList & extraList)
{
	BaseExtraList::Locker lock;

	auto * xOwner = extraList.Get <ExtraOwnership>();
	return xOwner ? xOwner->owner : nullptr;
}

static u32 GetOwningFactionRequiredRank(const BaseExtraList & extraList)
{
	BaseExtraList::Locker lock;

	auto * xRank = extraList.Get <ExtraRank>();
	return xRank ? xRank->rank : 0;
}

static void SetOwnerResult(double * result, TESForm * owner, const char * name)
{
	if(owner)
		SetFormResult(result, owner->refID);

	if(IsConsoleMode())
		Console_Print("%s >> %08X (%s)", name, owner ? owner->refID : 0, owner ? GetFullName(owner) : "none");
}

static bool Cmd_GetOwner_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj)
		SetOwnerResult(result, GetOwner(thisObj->extraList), "GetOwner");

	return true;
}

static bool Cmd_GetParentCellOwner_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj && thisObj->parentCell)
		SetOwnerResult(result, GetOwner(thisObj->parentCell->extraData), "GetParentCellOwner");

	return true;
}

static bool Cmd_GetOwningFactionRequiredRank_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj)
		*result = GetOwningFactionRequiredRank(thisObj->extraList);

	if(IsConsoleMode())
		Console_Print("GetOwningFactionRequiredRank >> %d", (int)*result);

	return true;
}

static bool Cmd_GetParentCellOwningFactionRequiredRank_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(thisObj && thisObj->parentCell)
		*result = GetOwningFactionRequiredRank(thisObj->parentCell->extraData);

	if(IsConsoleMode())
		Console_Print("GetParentCellOwningFactionRequiredRank >> %d", (int)*result);

	return true;
}

// The game's check of whether taking or using a reference is a crime for the player (classic
// TESObjectREFR::IsOffLimitsToPlayer): ownership, the player's faction ranks, rented-room globals
// and doors' own rules. The crosshair uses it to say "Steal" (0x06765C00).
typedef bool (* _IsOffLimitsToPlayer)(TESObjectREFR * refr);
static RelocAddr <_IsOffLimitsToPlayer> IsOffLimitsToPlayer(0x06582560);

static bool Cmd_IsOffLimits_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * actor = nullptr;
	if(!thisObj || !ExtractArgs(EXTRACT_ARGS, &actor)) return true;

	PlayerCharacter * player = PlayerCharacter::Get();
	if(actor && (!player || actor != player->baseForm))
	{
		// classic works out another actor's crimes itself, from its factions: not ported yet, so
		// fail the line, as an unported command does, rather than answer wrongly
		static bool s_reported = false;
		if(!s_reported)
		{
			s_reported = true;
			_MESSAGE("IsOffLimits for an actor other than the player is not implemented yet");
		}

		return false;
	}

	if(IsOffLimitsToPlayer(thisObj))
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsOffLimits >> %d", (int)*result);

	return true;
}

// The game's value of an item, what a theft's bounty is worked out from (0x06955040): the value
// component's for most items, and for potions and ingredients their magic item's, which works out
// an autocalc potion's value. -1 for a form with no value.
typedef s32 (* _GetItemValue)(TESForm * form);
static RelocAddr <_GetItemValue> GetItemValue(0x067D6A20);

static bool Cmd_GetGoldValue_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
	{
		s32 value = GetItemValue(form);
		if(value > 0)
			*result = value;
	}

	if(IsConsoleMode())
		Console_Print("GetGoldValue >> %d", (int)*result);

	return true;
}

static ParamInfo kParams_OneOptionalNPC[1] =
{
	{	"npc", kParamType_NPC, 1 },
};

// An actor base's flags: classic TESActorBaseData's, at +0x08 of that component (+0x48 in NPCs
// and creatures). Despite their names, IsRefEssential and SetRefEssential work on the base, as
// vanilla SetEssential does (0x06A18F20), which also marks the flags changed so a save keeps them.
enum
{
	kActorBaseFlag_Essential =	0x02,
	kActorBaseChanged_Flags =	0x10,
};

static u8 * GetActorBaseData(TESObjectREFR * refr)
{
	if(!refr || !refr->baseForm) return nullptr;

	return (u8 *)Runtime_DynamicCast(refr->baseForm, RTTI_TESForm, RTTI_TESActorBaseData);
}

static bool Cmd_IsRefEssential_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(u8 * data = GetActorBaseData(thisObj))
	{
		if(*(u32 *)(data + 0x08) & kActorBaseFlag_Essential)
			*result = 1;
	}

	if(IsConsoleMode())
		Console_Print("IsRefEssential >> %d", (int)*result);

	return true;
}

static bool Cmd_SetRefEssential_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 essential = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &essential)) return true;

	if(u8 * data = GetActorBaseData(thisObj))
	{
		u32 & flags = *(u32 *)(data + 0x08);
		if(essential)
			flags |= kActorBaseFlag_Essential;
		else
			flags &= ~kActorBaseFlag_Essential;

		typedef void (* _MarkChanged)(void * component, u32 changeFlags);
		auto markChanged = reinterpret_cast <_MarkChanged>((*(void ***)data)[0xA0 / 8]);
		markChanged(data, kActorBaseChanged_Flags);
	}

	return true;
}

static CommandInfo kCommandInfo_IsRefEssential =
{
	"IsRefEssential", "",
	0,
	"returns 1 if the calling reference is essential",
	1,
	0, nullptr,
	Cmd_IsRefEssential_Execute
};

static CommandInfo kCommandInfo_SetRefEssential =
{
	"SetRefEssential", "",
	0,
	"changes the calling reference to essential",
	1,
	1, kParams_OneInt,
	Cmd_SetRefEssential_Execute
};

// in Commands_Inventory.cpp, next to the equipped item lookup they use
extern CommandInfo kCommandInfo_GetObjectCharge;
extern CommandInfo kCommandInfo_GetEquippedCurrentCharge;
extern CommandInfo kCommandInfo_ModEquippedCurrentCharge;

static CommandInfo kCommandInfo_IsOffLimits =
{
	"IsOffLimits", "",
	0,
	"returns true if activating the calling reference would result in a crime for the actor",
	1,
	1, kParams_OneOptionalNPC,
	Cmd_IsOffLimits_Execute
};

static CommandInfo kCommandInfo_GetGoldValue =
{
	"GetGoldValue", "",
	0,
	"returns the gold value of the object or calling reference",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetGoldValue_Execute
};

static CommandInfo kCommandInfo_GetOwner =
{
	"GetOwner", "",
	0,
	"returns the owner of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwner_Execute
};

static CommandInfo kCommandInfo_GetParentCellOwner =
{
	"GetParentCellOwner", "",
	0,
	"returns the owner of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwner_Execute
};

static CommandInfo kCommandInfo_GetOwningFactionRequiredRank =
{
	"GetOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwningFactionRequiredRank_Execute
};

static CommandInfo kCommandInfo_GetParentCellOwningFactionRequiredRank =
{
	"GetParentCellOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwningFactionRequiredRank_Execute
};

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

static TESForm * FormFromValue(double value)
{
	u32 formID = FormIDFromValue(value);
	return formID ? LookupFormByID(formID) : nullptr;
}

// Expands a classic OBSE format string. OBSE's own specifiers are replaced in place, and any
// other % takes the next argument as a number, as classic OBSE left those to sprintf. %n and %i
// take a form, which a variable holds as its form ID's number. The ones that need a string or a
// name table (%c %p %k %v %z) still use up their arguments, so later numbers line up, but print
// "?" for now. False if there are too few arguments.
static bool FormatClassicString(const char * fmt, const double * args, u32 numArgs, std::string * out)
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

			case 'n':
			case 'N':
				// the form's name
				if(!nextArg(&value)) return false;
				*out += GetFullName(FormFromValue(value));
				p++;
				break;

			case 'i':
			case 'I':
			{
				// the form's ID, zeros for none
				if(!nextArg(&value)) return false;

				TESForm * form = FormFromValue(value);

				char buf[16];
				snprintf(buf, sizeof(buf), "%08X", form ? form->refID : 0);
				*out += buf;
				p++;
			}
			break;

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

ParamInfo kParams_FormatString[21] =
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

// Reads a command's format string and up to 20 numbers, as classic OBSE's PrintToConsole does.
// The numbers are read one at a time by the game's numeric argument reader, which keeps them as
// doubles, so a ref variable's form ID comes through whole: ExtractArgs would round them to floats.
bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out)
{
	u32 offset = *opcodeOffsetPtr;
	u32 end = offset + *(const u16 *)(scriptData + offset - 2);	// the arguments' length is just before them

	// the argument count comes first, and includes the format string
	u16 numArgs = *(const u16 *)(scriptData + offset);
	u16 fmtLen = *(const u16 *)(scriptData + offset + 2);
	offset += 4;

	if(!numArgs || numArgs > 21 || offset + fmtLen > end) return false;

	std::string fmt(scriptData + offset, fmtLen);
	offset += fmtLen;

	double args[20] = { 0 };
	for(u32 i = 0; i < u32(numArgs - 1); i++)
	{
		if(offset >= end || !ExtractNumericArg(&args[i], scriptData, &offset, thisObj, containingObj, script, locals, true))
			return false;
	}

	*opcodeOffsetPtr = offset;

	return FormatClassicString(fmt.c_str(), args, numArgs - 1, out);
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

// the runner Run's callers use, unless it's busy with another script (0x0694D580)
static RelocPtr <ScriptRunner *> g_scriptRunner(0x094904E8);

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

// The runner running this statement, and Run's length for it. False unless the command is a
// statement of its own run by the main runner: not part of an expression, nor in a script that
// another one started.
static bool GetStatementState(void * returnAddress, Script * script, const char * scriptData, u32 argsOffset,
	u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length)
{
	if(uintptr_t(returnAddress) != kStatementCommandReturn.getUIntPtr()) return false;

	*runner = *g_scriptRunner;
	if(!*runner || (*runner)->script != script) return false;

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
		_MESSAGE("%s in script %08X ignored: it only works as a statement of its own, in a script the game runs itself",
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
	if(!GetStatementState(returnAddress, script, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
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
	if(!GetStatementState(returnAddress, script, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
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

static ParamInfo kParams_OneOptionalInt[1] =
{
	{	"int", kParamType_Integer, 1 },
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
	{ &kCommandInfo_SaveIP,			kRetnType_Default },
	{ &kCommandInfo_RestoreIP,		kRetnType_Default },
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
