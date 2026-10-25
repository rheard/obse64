#include "CommandTable.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "Hooks_Script.h"
#include "ParamInfos.h"
#include "obse64_common/Log.h"
#include <cstring>
#include <set>

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
