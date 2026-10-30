#include "GameScript.h"
#include "Commands_Classic.h"
#include "GameBSExtraData.h"
#include "GameExtraData.h"
#include "GameMenus.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameConsole.h"
#include "Hooks_Script.h"
#include "InventoryReference.h"
#include "ParamInfos.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

class TESObjectWEAP;
class TESAmmo;

typedef std::vector <ExtraContainerChanges::Entry *> ExtraDataVec;
typedef std::map <TESForm *, size_t> ExtraContainerMap;

// unpack the linked list in to a vector/map
class ExtraContainerInfo
{
public:
	ExtraContainerInfo(BSSimpleList <ExtraContainerChanges::Entry *> * entryList)
	{
		m_vec.reserve(128);

		if(entryList)
		{
			for(auto * iter = &entryList->node; iter; iter = iter->m_next)
			{
				Accept(iter->m_data);
			}
		}
	}

	bool Accept(ExtraContainerChanges::Entry * data)
	{
		if(data)
		{
			m_vec.push_back(data);
			m_map[data->type] = m_vec.size() - 1;
		}

		return true;
	}

	bool IsValidContainerData(TESContainer::Entry * containerData, s32 & numObjects)
	{
		if(containerData) {
			numObjects = containerData->count;

			// make sure it isn't a leveled object
			if(DYNAMIC_CAST(containerData->type, TESForm, TESLevItem))
				return false;

			auto it = m_map.find(containerData->type);
			if(it != m_map.end()) {
				size_t index = it->second;
				if(auto extraData = m_vec[index])
				{
					numObjects += extraData->countDelta;

					// let's clear this item from the vector
					// this way we don't bother to look for it in the second step
					m_vec[index] = nullptr;
				}
			}

			// is at least one still here?
			if(numObjects > 0)
				return true;
		}
		return false;
	}

	ExtraDataVec		m_vec;
	ExtraContainerMap	m_map;
};

static TESForm * GetItemByIdx(TESObjectREFR * thisObj, u32 objIdx, s32 * outNumItems)
{
	if(!thisObj) return nullptr;

	u32 count = 0;

	auto * containerChanges = thisObj->extraList.Get<ExtraContainerChanges>();
	ExtraContainerInfo info(containerChanges ? containerChanges->GetObjList() : nullptr);

	TESContainer * container = nullptr;
	TESForm * baseForm = thisObj->GetBaseForm();
	if(baseForm)
		container = DYNAMIC_CAST(baseForm, TESForm, TESContainer);

	if(container)
	{
		for(auto * iter = &container->list.node; iter; iter = iter->m_next)
		{
			s32 numObjects = 0;
			if(info.IsValidContainerData(iter->m_data, numObjects))
			{
				if(count == objIdx)
				{
					if(outNumItems) *outNumItems = numObjects;
					return iter->m_data->type;
				}
				count++;
			}
		}
	}

	for(auto iter = info.m_vec.begin(); iter != info.m_vec.end(); ++iter)
	{
		auto * extraData = *iter;
		if(extraData && extraData->countDelta > 0)
		{
			if(count == objIdx)
			{
				if(outNumItems) *outNumItems = extraData->countDelta;
				return extraData->type;
			}
			count++;
		}
	}

	if(outNumItems) *outNumItems = 0;

	return nullptr;
}

bool Cmd_GetNumItems_Execute(COMMAND_ARGS)
{
	*result = 0;

	BaseExtraList::Locker lock;

	auto * containerChanges = thisObj->extraList.Get<ExtraContainerChanges>();
	ExtraContainerInfo info(containerChanges ? containerChanges->GetObjList() : nullptr);

	TESContainer * container = nullptr;
	TESForm * baseForm = thisObj->GetBaseForm();
	if(baseForm)
		container = DYNAMIC_CAST(baseForm, TESForm, TESContainer);

	u32 count = 0;

	if(container)
	{
		for(auto * iter = &container->list.node; iter; iter = iter->m_next)
		{
			s32 numObjects = 0;
			if(info.IsValidContainerData(iter->m_data, numObjects))
				count++;
		}
	}

	for(auto iter = info.m_vec.begin(); iter != info.m_vec.end(); ++iter)
	{
		auto * extraData = *iter;
		if(extraData && extraData->countDelta > 0)
			count++;
	}

	if(IsConsoleMode())
		Console_Print("GetNumItems: %d", count);

	*result = count;

	return true;
}

bool Cmd_GetInventoryItemType_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	u32 objIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &objIdx)) return true;

	BaseExtraList::Locker lock;

	u32 refID = 0;

	if(TESForm * type = GetItemByIdx(thisObj, objIdx, nullptr))
	{
		refID = type->refID;
		SetFormResult(result, refID);
	}

	if(IsConsoleMode())
		Console_Print("GetInventoryItemType: %d = %08X", objIdx, refID);

	return true;
}

enum
{
	// lower slots are TESBipedModelForm::kPart_*
	kSlot_RightRing = TESBipedModelForm::kPart_RightRing,
	kSlot_LeftRing,
	kSlot_Torch = TESBipedModelForm::kPart_Torch,	// 14
	kSlot_Weapon = TESBipedModelForm::kPart_Max,	// 16
	kSlot_Ammo,
};

bool ItemSlotMatches(TESForm * pForm, u32 slot)
{
	bool bMatches = false;
	if(pForm)
	{
		if(slot == kSlot_Weapon && DYNAMIC_CAST(pForm, TESForm, TESObjectWEAP))
			bMatches = true;
		else if(slot == kSlot_Ammo && DYNAMIC_CAST(pForm, TESForm, TESAmmo))
			bMatches = true;
		else if(slot == kSlot_Torch)
		{
			auto * pLight = DYNAMIC_CAST(pForm, TESForm, TESObjectLIGH);
			if(pLight && pLight->IsCarriable())
				bMatches = true;
		}
		else
		{
			if(auto * pBip = DYNAMIC_CAST(pForm, TESForm, TESBipedModelForm))
			{
				// if slot is an official slot match it
				if(slot < TESBipedModelForm::kPart_Max)
				{
					u32 inMask = TESBipedModelForm::MaskForSlot(slot);
					if((inMask & pBip->partMask) != 0)
						bMatches = true;
				}
				// if slot is an unofficial multi-slot - it must match exactly
				else if(pBip->GetSlot() == slot)
					bMatches = true;
			}
		}
	}
	return bMatches;
}

class FoundEquipped
{
public:
	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) = 0;
};

static inline bool IsRingSlot(u32 slot)
{
	return slot == kSlot_RightRing || slot == kSlot_LeftRing;
}

static bool FindEquipped(TESObjectREFR * thisObj, u32 slotIdx, FoundEquipped * foundEquippedFunctor, double * result)
{
	auto * containerChanges = thisObj->extraList.Get<ExtraContainerChanges>();
	if(containerChanges && containerChanges->data && containerChanges->data->objList)
	{
		for(auto * entry = &containerChanges->data->objList->node; entry; entry = entry->m_next)
		{
			// do the fast check first (an object must have extend data to be equipped)
			if(entry->m_data && entry->m_data->extraData && entry->m_data->type)
			{
				if(ItemSlotMatches(entry->m_data->type, slotIdx)) {
					// ok, it's the right type, now is it equipped?
					for(auto * extend = &entry->m_data->extraData->node; extend; extend = extend->m_next)
					{
						if(extend->m_data) {
							// handle rings
							bool bFound = false;
							if(IsRingSlot(slotIdx))
							{
								if(slotIdx == kSlot_LeftRing && extend->m_data->Contains(kExtraData_WornLeft))
								{
									bFound = true;
								}
								else if(slotIdx == kSlot_RightRing && extend->m_data->Contains(kExtraData_Worn))
								{
									bFound = true;
								}
							}
							else if(extend->m_data->Contains(kExtraData_Worn))
							{
								bFound = true;
							}

							if(bFound)
							{
								return foundEquippedFunctor->Found(entry->m_data, result, extend);
							}
						}
					}
				}
			}
		}
	}
	return false;
}

class feGetObject: public FoundEquipped
{
public:
	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		if(entryData) {
			SetFormResult(result, entryData->type->refID);
			return true;
		}
		return false;
	}
};

static bool Cmd_GetEquipmentSlotType_Execute(COMMAND_ARGS)
{
	*result = 0;

	// easy out if we don't have an object
	if(!thisObj) return true;

	u32 slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &slotIdx)) return true;
	feGetObject getObject;

	bool bFound = FindEquipped(thisObj, slotIdx, &getObject, result);
	if(IsConsoleMode())
	{
		if(!bFound)
			Console_Print("Nothing equipped in that slot.");
		else
		{
			TESForm * eqObj = LookupFormByID(FormIDFromValue(*result));
			if(eqObj)
				Console_Print("GetEquippedObject %d >> %s (%08x)", slotIdx, GetFullName(eqObj), eqObj->refID);
		}
	}

	return true;
}

CommandInfo kCommandInfo_GetNumItems =
{
	"GetNumItems", "gni",
	0,
	"return the number of items in an object's inventory",
	1,
	0, nullptr,
	Cmd_GetNumItems_Execute,
};

CommandInfo kCommandInfo_GetInventoryItemType =
{
	"GetInventoryItemType", "giit",
	0,
	"returns a ref to the type of the inventory item",
	1,
	1, kParams_OneInt,
	Cmd_GetInventoryItemType_Execute
};

// classic OBSE's commands about base objects: the type checks, values, quest items and model paths
// (Commands_Classic.cpp registers them)

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

CommandInfo kCommandInfo_GetBaseObject =
{
	"GetBaseObject", "",
	0,
	"returns the base object of the reference",
	1,
	0, nullptr,
	Cmd_GetBaseObject_Execute
};

static bool Cmd_GetObjectType_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
		*result = form->typeID;

	if(IsConsoleMode())
		Console_Print("GetObjectType >> %d", (int)*result);

	return true;
}

CommandInfo kCommandInfo_GetObjectType =
{
	"GetObjectType", "",
	0,
	"returns the type id of the specified object or the reference on which it was called",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetObjectType_Execute
};

// With anyForm, the argument may be any form, as classic took for IsWeapon and the rest of that
// family; IsCreature's it read as the game does, so an actor base only.
bool IsFormType_Execute(COMMAND_ARGS, u8 typeID, const char * name, bool anyForm)
{
	*result = 0;

	TESForm * form = nullptr;
	if(anyForm)
		ExtractArgsAnyForm(EXTRACT_ARGS, &form);
	else
		ExtractArgs(EXTRACT_ARGS, &form);

	form = FormArgOrBase(form, thisObj);
	if(form && form->typeID == typeID)
		*result = 1;

	if(IsConsoleMode())
		Console_Print("%s >> %d", name, (int)*result);

	return true;
}

#define IS_FORM_TYPE(name, type, what)													\
	static bool Cmd_##name##_Execute(COMMAND_ARGS)										\
	{																					\
		return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_##type, #name, true);	\
	}																					\
																						\
	CommandInfo kCommandInfo_##name =													\
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

// The game's value of an item, what a theft's bounty is worked out from (0x06955040): the value
// component's for most items, and for potions and ingredients their magic item's, which works out
// an autocalc potion's value. -1 for a form with no value.
typedef s32 (* _GetItemValue)(TESForm * form);
static RelocAddr <_GetItemValue> GetItemValue(0x067D6A20);

static bool Cmd_GetGoldValue_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &form)) return true;

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

CommandInfo kCommandInfo_GetGoldValue =
{
	"GetGoldValue", "",
	0,
	"returns the gold value of the object or calling reference",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetGoldValue_Execute
};

// A quest item is a base object with form flag 0x400 (TESForm::IsQuestItem). SetQuestItem also
// marks the flags changed, so a save keeps the change, as vanilla SetQuestObject (0x06A1AEA0) does.
static bool Cmd_IsQuestItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form && form->IsQuestItem())
		*result = 1;

	if(IsConsoleMode())
		Console_Print("IsQuestItem >> %d", (int)*result);

	return true;
}

static bool Cmd_SetQuestItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 questItem = 0;
	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &questItem, &form)) return true;

	form = FormArgOrBase(form, thisObj);
	if(form)
		form->SetQuestItem(questItem != 0);

	return true;
}

static ParamInfo kParams_SetObjectInteger[2] =
{
	{	"value", kParamType_Integer, 0 },
	{	"type", kParamType_InventoryObject, 1 },
};

CommandInfo kCommandInfo_IsQuestItem =
{
	"IsQuestItem", "",
	0,
	"returns 1 if the object or calling reference is a quest item",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_IsQuestItem_Execute
};

CommandInfo kCommandInfo_SetQuestItem =
{
	"SetQuestItem", "",
	0,
	"Sets whether the specified object or object reference is a quest item.",
	0,
	2, kParams_SetObjectInteger,
	Cmd_SetQuestItem_Execute
};


// A model's path is the BSString at +0x08 of TESModel. Armor and clothing have no TESModel: their
// models are in the biped model component, where the game loads the male ground (world) model,
// MOD2, into +0x70 (0x06820F40). Paths are set with the game's string setter, which is all
// TESModel's own setter does (0x067D6740).
typedef bool (* _BSString_Set)(BSStringT * str, const char * src, u32 minLength);
static RelocAddr <_BSString_Set> BSString_Set(0x06562E40);

static BSStringT * ModelPathOf(TESForm * form, bool maleGround)
{
	if(!form) return nullptr;

	if(maleGround)
	{
		auto * biped = DYNAMIC_CAST(form, TESForm, TESBipedModelForm);
		return biped ? &biped->groundModel[0].nifPath : nullptr;
	}

	auto * model = DYNAMIC_CAST(form, TESForm, TESModel);
	return model ? &model->nifPath : nullptr;
}

// classic's ModelPathIncludes and friends: is text part of the path, ignoring case
static bool ComparePath_Execute(COMMAND_ARGS, bool maleGround, const char * name)
{
	*result = 0;

	char text[BUFSIZ] = { 0 };
	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &text, &form)) return true;

	BSStringT * path = ModelPathOf(FormArgOrBase(form, thisObj), maleGround);
	if(path && path->m_data && *text)
	{
		std::string haystack(path->m_data), needle(text);
		for(char & c : haystack) c = char(tolower(u8(c)));
		for(char & c : needle) c = char(tolower(u8(c)));

		if(haystack.find(needle) != std::string::npos)
			*result = 1;
	}

	if(IsConsoleMode())
		Console_Print("%s >> %d", name, (int)*result);

	return true;
}

static bool CopyPath_Execute(COMMAND_ARGS, bool maleGround)
{
	*result = 0;

	TESForm * src = nullptr;
	TESForm * dest = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &src, &dest)) return true;

	BSStringT * srcPath = ModelPathOf(FormArgOrBase(src, nullptr), maleGround);
	BSStringT * destPath = ModelPathOf(FormArgOrBase(dest, thisObj), maleGround);
	if(srcPath && destPath && srcPath != destPath)
		BSString_Set(destPath, srcPath->m_data, 0);

	return true;
}

static bool Cmd_CompareModelPath_Execute(COMMAND_ARGS)
{
	return ComparePath_Execute(PASS_COMMAND_ARGS, false, "CompareModelPath");
}

static bool Cmd_CompareMaleGroundPath_Execute(COMMAND_ARGS)
{
	return ComparePath_Execute(PASS_COMMAND_ARGS, true, "CompareMaleGroundPath");
}

static bool Cmd_CopyModelPath_Execute(COMMAND_ARGS)
{
	return CopyPath_Execute(PASS_COMMAND_ARGS, false);
}

static bool Cmd_CopyMaleGroundPath_Execute(COMMAND_ARGS)
{
	return CopyPath_Execute(PASS_COMMAND_ARGS, true);
}

static ParamInfo kParams_SetObjectString[2] =
{
	{	"value", kParamType_String, 0 },
	{	"type", kParamType_InventoryObject, 1 },
};

static ParamInfo kParams_CopyObjectPath[2] =
{
	{	"src", kParamType_InventoryObject, 0 },
	{	"dest", kParamType_InventoryObject, 1 },
};

CommandInfo kCommandInfo_CompareModelPath =
{
	"CompareModelPath", "",
	0,
	"Returns 1 if the given string is part of the model path.",
	0,
	2, kParams_SetObjectString,
	Cmd_CompareModelPath_Execute
};

CommandInfo kCommandInfo_CompareMaleGroundPath =
{
	"CompareMaleGroundPath", "",
	0,
	"Returns 1 if the given string is part of the male ground path.",
	0,
	2, kParams_SetObjectString,
	Cmd_CompareMaleGroundPath_Execute
};

CommandInfo kCommandInfo_CopyModelPath =
{
	"CopyModelPath", "",
	0,
	"copies the model path from one object to another",
	0,
	2, kParams_CopyObjectPath,
	Cmd_CopyModelPath_Execute
};

CommandInfo kCommandInfo_CopyMaleGroundPath =
{
	"CopyMaleGroundPath", "",
	0,
	"copies the male ground path from one object to another",
	0,
	2, kParams_CopyObjectPath,
	Cmd_CopyMaleGroundPath_Execute
};

// classic OBSE's AddItem, RemoveItem, EquipItem and UnequipItem without the game's messages
// (Commands_Classic.cpp registers them)

// AddItem tells the player what was added with the game's message that has an icon (classic's
// QueueUIMessage_2, 0x0656C3E0), called from 0x067DD888, and RemoveItem what was removed with the
// same, from its own handler at 0x06A15F8B. AddItemNS and RemoveItemNS run the game's command with
// that message skipped, as classic did by making the message functions return at once.
static RelocAddr <uintptr_t> kAddItem_MessageCall(0x067DD888);
static RelocAddr <uintptr_t> kRemoveItem_MessageCall(0x06A15F8B);

// EquipItem and UnequipItem tell the player what was equipped with the game's plain message, a
// notification (classic's QueueUIMessage, 0x0656C6B0), called from 0x06A09E6D and 0x06A20D6F,
// which EquipItemNS and UnequipItemNS skip the same way.
static RelocAddr <uintptr_t> kEquipItem_MessageCall(0x06A09E6D);
static RelocAddr <uintptr_t> kUnequipItem_MessageCall(0x06A20D6F);

enum
{
	kOpcode_AddItem =		0x1002,
	kOpcode_RemoveItem =	0x1052,
	kOpcode_EquipItem =		0x10EE,
	kOpcode_UnequipItem =	0x10EF,
};

static u32 s_itemMessagesOff = 0;

static bool IconMessage_Hook(void * message, float duration, const char * icon, void * sound, u8 unk)
{
	if(s_itemMessagesOff) return false;

	return QueueUIMessageWithIcon(message, duration, icon, sound, unk);
}

static bool EquipItem_Message_Hook(UIMessage * message, void * unk, u32 notification, float duration)
{
	if(s_itemMessagesOff) return false;

	return QueueUIMessage(message, unk, notification, duration);
}

void InstallItemMessageHook()
{
	if(IsCallTo(kAddItem_MessageCall.getUIntPtr(), QueueUIMessageWithIcon.getUIntPtr()))
		g_branchTrampoline.write5Call(kAddItem_MessageCall.getUIntPtr(), uintptr_t(IconMessage_Hook));
	else
		_ERROR("AddItem's message call isn't where expected; AddItemNS will show the message");

	if(IsCallTo(kRemoveItem_MessageCall.getUIntPtr(), QueueUIMessageWithIcon.getUIntPtr()))
		g_branchTrampoline.write5Call(kRemoveItem_MessageCall.getUIntPtr(), uintptr_t(IconMessage_Hook));
	else
		_ERROR("RemoveItem's message call isn't where expected; RemoveItemNS will show the message");

	for(uintptr_t call : { kEquipItem_MessageCall.getUIntPtr(), kUnequipItem_MessageCall.getUIntPtr() })
	{
		if(IsCallTo(call, QueueUIMessage.getUIntPtr()))
			g_branchTrampoline.write5Call(call, uintptr_t(EquipItem_Message_Hook));
		else
			_ERROR("an equip message call isn't where expected; EquipItemNS and UnequipItemNS will show it");
	}
}

// runs the game's command with its item messages skipped
static bool RunWithoutItemMessages(u32 opcode, COMMAND_ARGS)
{
	CommandInfo * cmd = GetCommandInfo(opcode);
	if(!cmd || !cmd->execute) return false;

	s_itemMessagesOff++;
	bool ok = cmd->execute(cmd->params, scriptData, thisObj, containingObj, script, locals, result, opcodeOffsetPtr);
	s_itemMessagesOff--;

	return ok;
}

static bool Cmd_AddItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_AddItem, PASS_COMMAND_ARGS);
}

static bool Cmd_RemoveItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_RemoveItem, PASS_COMMAND_ARGS);
}

static bool Cmd_EquipItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_EquipItem, PASS_COMMAND_ARGS);
}

static bool Cmd_UnequipItemNS_Execute(COMMAND_ARGS)
{
	return RunWithoutItemMessages(kOpcode_UnequipItem, PASS_COMMAND_ARGS);
}

static ParamInfo kParams_AddItem[2] =
{
	{	"item", kParamType_InventoryObject, 0 },
	{	"quantity", kParamType_Integer, 0 },
};

CommandInfo kCommandInfo_AddItemNS =
{
	"AddItemNS", "",
	0,
	"version of AddItem which doesn't generate UI messages",
	1,
	2, kParams_AddItem,
	Cmd_AddItemNS_Execute
};

CommandInfo kCommandInfo_RemoveItemNS =
{
	"RemoveItemNS", "",
	0,
	"version of RemoveItem which doesn't generate UI messages",
	1,
	2, kParams_AddItem,
	Cmd_RemoveItemNS_Execute
};

static ParamInfo kParams_EquipItem[2] =
{
	{	"item", kParamType_InventoryObject, 0 },
	{	"lockEquip", kParamType_Integer, 1 },
};

CommandInfo kCommandInfo_EquipItemNS =
{
	"EquipItemNS", "",
	0,
	"version of EquipItem which doesn't generate UI messages",
	1,
	2, kParams_EquipItem,
	Cmd_EquipItemNS_Execute
};

CommandInfo kCommandInfo_UnequipItemNS =
{
	"UnequipItemNS", "",
	0,
	"version of UnequipItem which doesn't generate UI messages",
	1,
	2, kParams_EquipItem,
	Cmd_UnequipItemNS_Execute
};

// classic OBSE's enchantment charge commands (Commands_Classic.cpp registers them)

// new extra data is made with the game's allocator and a constructor, as vanilla SetOwnership makes
// its own (0x06621210), and given to a list with the list's own add
typedef ExtraCharge * (* _ExtraCharge_Construct)(void * mem, float charge);
static RelocAddr <_ExtraCharge_Construct> ExtraCharge_Construct(0x067FCD20);

// an object's full charge, from its enchantable component; valid for weapons
static u32 GetFullCharge(TESForm * form)
{
	auto * enchantable = form ? DYNAMIC_CAST(form, TESForm, TESEnchantableForm) : nullptr;
	return enchantable ? enchantable->enchantment : 0;
}

static float GetChargeOf(BaseExtraList * list, TESForm * item)
{
	auto * xCharge = list ? list->Get<ExtraCharge>() : nullptr;
	return xCharge ? xCharge->charge : float(GetFullCharge(item));
}

// As classic: back at full, the item loses its ExtraCharge, as a fully charged item has none; any
// other value is kept. A list isn't given an ExtraCharge for an item that can't be enchanted. True
// if the list changed, so not for full charge on an item already at full.
static bool SetChargeOf(BaseExtraList * list, TESForm * item, float charge)
{
	if(!list || !DYNAMIC_CAST(item, TESForm, TESEnchantableForm)) return false;

	auto * xCharge = list->Get<ExtraCharge>();
	if(charge == float(GetFullCharge(item)))
	{
		if(!xCharge) return false;

		BaseExtraList_Remove(list, xCharge, true);
	}
	else if(xCharge)
		xCharge->charge = charge;
	else if(void * mem = GameHeap_Allocate(sizeof(ExtraCharge)))
		BaseExtraList_Add(list, ExtraCharge_Construct(mem, charge));
	else
		return false;

	return true;
}

static bool Cmd_GetObjectCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &form)) return true;

	// a reference stands for its base object, and no argument for the calling reference's
	if(auto * refr = form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr)
		form = refr->baseForm;
	if(!form && thisObj)
		form = thisObj->baseForm;

	*result = GetFullCharge(form);

	if(IsConsoleMode())
		Console_Print("GetObjectCharge >> %.0f", *result);

	return true;
}

class feGetCurrentCharge : public FoundEquipped
{
public:
	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		*result = GetChargeOf(extendData->m_data, entryData->type);
		return true;
	}
};

static bool Cmd_GetEquippedCurrentCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	u32 slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feGetCurrentCharge getCharge;
	FindEquipped(thisObj, slotIdx, &getCharge, result);

	if(IsConsoleMode())
		Console_Print("GetEquippedCurrentCharge %d >> %.2f", slotIdx, *result);

	return true;
}

// As classic: the charge goes up or down by the amount, not below 0; back at full, the item
// loses its ExtraCharge, as a fully charged item has none.
class feModCurrentCharge : public FoundEquipped
{
	float	m_amount;

public:
	feModCurrentCharge(float amount) : m_amount(amount) { }

	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		float charge = GetChargeOf(extendData->m_data, entryData->type) + m_amount;
		if(charge < 0)
			charge = 0;

		SetChargeOf(extendData->m_data, entryData->type, charge);
		return true;
	}
};

static bool Cmd_ModEquippedCurrentCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	float amount = 0;
	u32 slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &amount, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feModCurrentCharge modCharge(amount);
	FindEquipped(thisObj, slotIdx, &modCharge, result);

	return true;
}

// GetCurrentCharge, SetCurrentCharge and ModCurrentCharge, on an item in the world or one in an
// inventory, through an inventory reference (ForEach's), whose stack's own extra data they read and
// change. An inventory reference's own copy is read when its stack has no list of its own (so is
// at full), or is gone.
static bool Cmd_GetCurrentCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	BaseExtraList::Locker lock;

	BaseExtraList * list = IsInventoryRef(thisObj) ? GetInventoryRefList(thisObj, false) : nullptr;
	if(!list)
		list = &thisObj->extraList;

	*result = GetChargeOf(list, thisObj->baseForm);

	if(IsConsoleMode())
		Console_Print("GetCurrentCharge >> %.2f", *result);

	return true;
}

// As classic: back at full, the item loses its ExtraCharge; a charge below 0 or above full changes
// nothing. A stack of items without extra data of their own is given a list for its new charge.
static bool ChangeCurrentCharge(COMMAND_ARGS, bool mod)
{
	*result = 0;

	float value = 0;
	if(!thisObj || !ExtractArgs(EXTRACT_ARGS, &value)) return true;

	TESForm * item = thisObj->baseForm;
	if(!item || !DYNAMIC_CAST(item, TESForm, TESEnchantableForm)) return true;

	BaseExtraList::Locker lock;

	bool isInventoryRef = IsInventoryRef(thisObj);
	BaseExtraList * list = isInventoryRef ? GetInventoryRefList(thisObj, false) : &thisObj->extraList;

	float fullCharge = float(GetFullCharge(item));
	float charge = (mod ? GetChargeOf(list, item) : 0) + value;
	if(!(charge >= 0 && charge <= fullCharge)) return true;

	// a stack of items without extra data of their own needs a list only for a charge below full
	if(!list && charge != fullCharge)
		list = GetInventoryRefList(thisObj, true);

	if(SetChargeOf(list, item, charge))
	{
		*result = 1;
		if(isInventoryRef)
			RefreshInventoryRef(thisObj);
	}

	return true;
}

static bool Cmd_SetCurrentCharge_Execute(COMMAND_ARGS)
{
	return ChangeCurrentCharge(PASS_COMMAND_ARGS, false);
}

static bool Cmd_ModCurrentCharge_Execute(COMMAND_ARGS)
{
	return ChangeCurrentCharge(PASS_COMMAND_ARGS, true);
}

// classic OBSE's item health and weapon poison commands, and SetEquippedCurrentCharge (Commands_Classic.cpp
// registers them)

// an item's health is its ExtraHealth's while it has one (0x067FD010 makes one), else its base's
typedef ExtraHealth * (* _ExtraHealth_Construct)(void * mem, float health);
static RelocAddr <_ExtraHealth_Construct> ExtraHealth_Construct(0x067FD010);

// the game's own: give a list a poison, changing the one it has or adding an ExtraPoison
// (0x067FD3C0), as using a poison on the weapon in hand does (0x06601EA0)
typedef void (* _ExtraDataList_SetPoison)(BaseExtraList * list, TESForm * poison);
static RelocAddr <_ExtraDataList_SetPoison> ExtraDataList_SetPoison(0x06621570);

// an object's full health, from its health component; weapons and armor have one
static u32 GetFullHealth(TESForm * form)
{
	auto * health = form ? DYNAMIC_CAST(form, TESForm, TESHealthForm) : nullptr;
	return health ? health->health : 0;
}

static bool Cmd_GetObjectHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgsAnyForm(EXTRACT_ARGS, &form)) return true;

	// a reference stands for its base object, and no argument for the calling reference's
	if(auto * refr = form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr)
		form = refr->baseForm;
	if(!form && thisObj)
		form = thisObj->baseForm;

	*result = GetFullHealth(form);

	if(IsConsoleMode())
		Console_Print("GetObjectHealth >> %.0f", *result);

	return true;
}

static float GetHealthOf(BaseExtraList * list, TESForm * item)
{
	auto * xHealth = list ? list->Get<ExtraHealth>() : nullptr;
	return xHealth ? xHealth->health : float(GetFullHealth(item));
}

// As classic: back at full, the item loses its ExtraHealth; any other value is kept, above full
// too. A list isn't given an ExtraHealth for an item without a health component. True if the
// list changed, so not for full health on an item already at full.
static bool SetHealthOf(BaseExtraList * list, TESForm * item, float health)
{
	if(!list || !DYNAMIC_CAST(item, TESForm, TESHealthForm)) return false;

	auto * xHealth = list->Get<ExtraHealth>();
	if(health == float(GetFullHealth(item)))
	{
		if(!xHealth) return false;

		BaseExtraList_Remove(list, xHealth, true);
	}
	else if(xHealth)
		xHealth->health = health;
	else if(void * mem = GameHeap_Allocate(sizeof(ExtraHealth)))
		BaseExtraList_Add(list, ExtraHealth_Construct(mem, health));
	else
		return false;

	return true;
}

class feGetCurrentHealth : public FoundEquipped
{
public:
	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		*result = GetHealthOf(extendData->m_data, entryData->type);
		return true;
	}
};

static bool Cmd_GetEquippedCurrentHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	u32 slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feGetCurrentHealth getHealth;
	FindEquipped(thisObj, slotIdx, &getHealth, result);

	if(IsConsoleMode())
		Console_Print("GetEquippedCurrentHealth %d >> %.2f", slotIdx, *result);

	return true;
}

// As classic: SetEquippedCurrentHealth gives a whole number; ModEquippedCurrentHealth adds to the
// health, not below 0.
class feChangeCurrentHealth : public FoundEquipped
{
	float	m_value;
	bool	m_mod;

public:
	feChangeCurrentHealth(float value, bool mod) : m_value(value), m_mod(mod) { }

	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		float health = m_value;
		if(m_mod)
		{
			health += GetHealthOf(extendData->m_data, entryData->type);
			if(health < 0)
				health = 0;
		}

		SetHealthOf(extendData->m_data, entryData->type, health);
		return true;
	}
};

static bool Cmd_SetEquippedCurrentHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	u32 health = 0, slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &health, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feChangeCurrentHealth setHealth(float(health), false);
	FindEquipped(thisObj, slotIdx, &setHealth, result);

	return true;
}

static bool Cmd_ModEquippedCurrentHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	float amount = 0;
	u32 slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &amount, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feChangeCurrentHealth modHealth(amount, true);
	FindEquipped(thisObj, slotIdx, &modHealth, result);

	return true;
}

// GetCurrentHealth and SetCurrentHealth, on an item in the world or, through an inventory
// reference, one in an inventory, as the charge commands above are
static bool Cmd_GetCurrentHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	BaseExtraList::Locker lock;

	BaseExtraList * list = IsInventoryRef(thisObj) ? GetInventoryRefList(thisObj, false) : nullptr;
	if(!list)
		list = &thisObj->extraList;

	*result = GetHealthOf(list, thisObj->baseForm);

	if(IsConsoleMode())
		Console_Print("GetCurrentHealth >> %.2f", *result);

	return true;
}

static bool Cmd_SetCurrentHealth_Execute(COMMAND_ARGS)
{
	*result = 0;

	float health = 0;
	if(!thisObj || !ExtractArgs(EXTRACT_ARGS, &health)) return true;

	TESForm * item = thisObj->baseForm;
	if(!item || !DYNAMIC_CAST(item, TESForm, TESHealthForm)) return true;

	BaseExtraList::Locker lock;

	bool isInventoryRef = IsInventoryRef(thisObj);
	BaseExtraList * list = isInventoryRef ? GetInventoryRefList(thisObj, false) : &thisObj->extraList;

	// a stack of items without extra data of their own is at full, and needs a list only for a
	// health below or above it
	if(!list && health != float(GetFullHealth(item)))
		list = GetInventoryRefList(thisObj, true);

	// 1 if the item's health changed, as SetCurrentCharge gives 1 if its charge did (classic's
	// SetCurrentHealth gave 0 either way)
	if(SetHealthOf(list, item, health))
	{
		*result = 1;
		if(isInventoryRef)
			RefreshInventoryRef(thisObj);
	}

	return true;
}

// As classic, a whole number, set as it's given: full takes the ExtraCharge away.
class feSetCurrentCharge : public FoundEquipped
{
	float	m_charge;

public:
	feSetCurrentCharge(float charge) : m_charge(charge) { }

	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		SetChargeOf(extendData->m_data, entryData->type, m_charge);
		return true;
	}
};

static bool Cmd_SetEquippedCurrentCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	u32 charge = 0, slotIdx = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &charge, &slotIdx)) return true;

	BaseExtraList::Locker lock;

	feSetCurrentCharge setCharge((float)charge);
	FindEquipped(thisObj, slotIdx, &setCharge, result);

	return true;
}

// The poison on the weapon in hand. GetEquippedWeaponPoison gives it; SetEquippedWeaponPoison puts
// one there, in place of any other; RemoveEquippedWeaponPoison takes it off and gives it.
class feWeaponPoison : public FoundEquipped
{
	TESForm	* m_poison;	// nullptr: only read it
	bool	m_remove;

public:
	feWeaponPoison(TESForm * poison, bool remove) : m_poison(poison), m_remove(remove) { }

	virtual bool Found(ExtraContainerChanges::Entry * entryData, double * result, BSSimpleList <ExtraDataList *>::Node * extendData) {
		if(!DYNAMIC_CAST(entryData->type, TESForm, TESObjectWEAP)) return true;

		ExtraDataList * list = extendData->m_data;
		auto * xPoison = list->Get<ExtraPoison>();
		if(xPoison && xPoison->poison)
			SetFormResult(result, xPoison->poison->refID);

		if(m_remove)
		{
			if(xPoison)
				BaseExtraList_Remove(list, xPoison, true);
		}
		else if(m_poison)
			ExtraDataList_SetPoison(list, m_poison);

		return true;
	}
};

static bool Cmd_GetEquippedWeaponPoison_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	BaseExtraList::Locker lock;

	feWeaponPoison getPoison(nullptr, false);
	FindEquipped(thisObj, kSlot_Weapon, &getPoison, result);

	if(IsConsoleMode())
	{
		TESForm * poison = *result ? LookupFormByID(FormIDFromValue(*result)) : nullptr;
		Console_Print("GetEquippedWeaponPoison >> %s (%08X)", poison ? GetFullName(poison) : "none", poison ? poison->refID : 0);
	}

	return true;
}

// The poison argument is a magic item, as classic's is (ExtractArgs gives its MagicItem part); a
// weapon's ExtraPoison holds the poison itself, as a form, so the item is cast back to one.
static bool Cmd_SetEquippedWeaponPoison_Execute(COMMAND_ARGS)
{
	*result = 0;

	void * magicItem = nullptr;
	if(!thisObj || !ExtractArgs(EXTRACT_ARGS, &magicItem) || !magicItem) return true;

	if(!Runtime_DynamicCast(magicItem, RTTI_MagicItem, RTTI_AlchemyItem)) return true;
	auto * poison = (TESForm *)Runtime_DynamicCast(magicItem, RTTI_MagicItem, RTTI_TESForm);
	if(!poison) return true;

	BaseExtraList::Locker lock;

	feWeaponPoison setPoison(poison, false);
	FindEquipped(thisObj, kSlot_Weapon, &setPoison, result);

	*result = 0;	// classic's return here is the old poison's form ID in a number's place

	return true;
}

static bool Cmd_RemoveEquippedWeaponPoison_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	BaseExtraList::Locker lock;

	feWeaponPoison removePoison(nullptr, true);
	FindEquipped(thisObj, kSlot_Weapon, &removePoison, result);

	return true;
}

// classic OBSE's quick key commands (Commands_Classic.cpp registers them)

// The player's eight quick keys, kept as classic Oblivion kept them: a list of forms for each key,
// holding one form in practice, as the game's menus replace what's there (0x067248F0), and saved
// by form ID. An item on a key also has an ExtraQuickKey with the key's index on its stack in the
// player's inventory: pressing the key uses the stack carrying it (0x0663FD40 finds it), and the
// next key pressed takes an item with no such stack off its key (0x06724B90), as it does a spell
// the player doesn't have. The Unreal quick key menus show the lists, read again whenever one opens
// and after each key pressed.

// 20 NiTPointerList <TESForm>
struct QuickKeyList
{
	struct Node
	{
		Node	* next;	// 00
		Node	* prev;	// 08
		TESForm	* form;	// 10
	};

	void	** vtbl;	// 00 +08 allocates a node, +10 frees one
	Node	* start;	// 08
	Node	* end;		// 10
	u32		numItems;	// 18
	u32		pad1C;		// 1C
};

static_assert(sizeof(QuickKeyList) == 0x20);

enum { kNumQuickKeys = 8 };

static RelocPtr <QuickKeyList> g_quickKeys(0x09483210);

typedef void (* _QuickKeyList_RemoveAll)(QuickKeyList * list);
static RelocAddr <_QuickKeyList_RemoveAll> QuickKeyList_RemoveAll(0x065626C0);

typedef QuickKeyList::Node * (* _QuickKeyList_AllocateNode)(QuickKeyList * list);
enum { kQuickKeyList_AllocateNode = 1 };

// The game's own, as its menus use them: mark an item's stack with a key (a stack without a list of
// its own is given one), or take a key's mark off an item's stacks. Both tell the Unreal inventory
// (Register_QuickKey_Item). Marking also marks the copy of a worn item the actor's process keeps,
// and refuses a stack that can't be worn (a bound item's), saying so on screen.
typedef void (* _InventoryChanges_SetQuickKey)(ExtraContainerChanges::Data * changes, TESForm * item, ExtraDataList * list, u8 key);
static RelocAddr <_InventoryChanges_SetQuickKey> InventoryChanges_SetQuickKey(0x06649070);
typedef void (* _InventoryChanges_ClearQuickKey)(ExtraContainerChanges::Data * changes, TESForm * item, s32 key);
static RelocAddr <_InventoryChanges_ClearQuickKey> InventoryChanges_ClearQuickKey(0x066471E0);

static ExtraContainerChanges::Data * GetPlayerInventoryChanges()
{
	TESObjectREFR * player = PlayerCharacter::Get();
	auto * changes = player ? player->extraList.Get<ExtraContainerChanges>() : nullptr;
	return changes ? changes->data : nullptr;
}

// takes what's on a key (0-7) off it, and its mark off the item's stacks, as the game does before
// putting something else there
static void ClearQuickKey(u32 key)
{
	QuickKeyList & keyList = ((QuickKeyList *)g_quickKeys)[key];
	auto * changes = GetPlayerInventoryChanges();

	for(auto * node = keyList.start; node; node = node->next)
		if(node->form && node->form->typeID != kFormType_Spell && changes)
			InventoryChanges_ClearQuickKey(changes, node->form, key);

	QuickKeyList_RemoveAll(&keyList);
}

// Puts a form on a key (0-7) in place of what's there, and for an item, marks the player's stack of
// it: one worn, else the first, as classic marked the first. An item can work from one key only, so
// it's taken off any other; a spell can be on several, as the game's menus allow.
static void SetQuickKey(u32 key, TESForm * form)
{
	ClearQuickKey(key);

	QuickKeyList * keys = g_quickKeys;
	bool isSpell = form->typeID == kFormType_Spell;

	if(!isSpell)
	{
		for(u32 other = 0; other < kNumQuickKeys; other++)
		{
			for(auto * node = keys[other].start; node; node = node->next)
			{
				if(node->form == form)
				{
					ClearQuickKey(other);
					break;
				}
			}
		}
	}

	QuickKeyList & keyList = keys[key];
	auto allocateNode = reinterpret_cast <_QuickKeyList_AllocateNode>(keyList.vtbl[kQuickKeyList_AllocateNode]);
	auto * node = allocateNode(&keyList);
	if(!node) return;

	node->form = form;
	node->next = nullptr;
	node->prev = keyList.end;
	if(keyList.end)
		keyList.end->next = node;
	else
		keyList.start = node;
	keyList.end = node;
	keyList.numItems++;

	if(isSpell) return;

	auto * changes = GetPlayerInventoryChanges();
	if(!changes) return;

	std::vector <InventoryStack> stacks;
	GetInventoryStacks(PlayerCharacter::Get(), &stacks);

	const InventoryStack * marked = nullptr;
	for(const auto & stack : stacks)
	{
		if(stack.item != form || stack.count <= 0) continue;

		if(!marked)
			marked = &stack;

		if(stack.list && (stack.list->Contains(kExtraData_Worn) || stack.list->Contains(kExtraData_WornLeft)))
		{
			marked = &stack;
			break;
		}
	}

	// As classic, an item the player doesn't have stays on the key; the next key pressed takes it off.
	// The game marks no stack that can't be worn, and would say so on screen; this says nothing.
	if(!marked)
	{
		if(IsConsoleMode())
			Console_Print("SetHotKeyItem >> Item not found in inventory");
	}
	else if(!marked->list || !marked->list->Contains(kExtraData_CannotWear))
		InventoryChanges_SetQuickKey(changes, form, marked->list, key);
}

static bool Cmd_GetHotKeyItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 key = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &key)) return true;

	if(key >= 1 && key <= kNumQuickKeys)
	{
		BaseExtraList::Locker lock;

		const QuickKeyList & keyList = ((QuickKeyList *)g_quickKeys)[key - 1];
		TESForm * form = keyList.start ? keyList.start->form : nullptr;
		if(form)
		{
			SetFormResult(result, form->refID);

			if(IsConsoleMode())
				Console_Print("GetHotKeyItem %d >> %s (%08X)", key, GetFullName(form), form->refID);
		}
	}

	return true;
}

static bool Cmd_ClearHotKey_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 key = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &key)) return true;

	if(key >= 1 && key <= kNumQuickKeys)
	{
		BaseExtraList::Locker lock;
		ClearQuickKey(key - 1);
	}

	return true;
}

// classic's parameters, whose item lets only an item through, though the command is for spells too
static ParamInfo kParams_SetHotKeyItem[2] =
{
	{ "hotkey", kParamType_Integer, 0 },
	{ "item", kParamType_InventoryObject, 0 },
};

// what SetHotKeyItem reads its arguments with again when they aren't an item, for a spell
static ParamInfo kParams_SetHotKeySpell[2] =
{
	{ "hotkey", kParamType_Integer, 0 },
	{ "spell", kParamType_SpellItem, 0 },
};

static bool Cmd_SetHotKeyItem_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 key = 0;
	TESForm * form = nullptr;

	u32 argsOffset = *opcodeOffsetPtr;
	if(!ExtractArgs(EXTRACT_ARGS, &key, &form))
	{
		*opcodeOffsetPtr = argsOffset;
		form = nullptr;
		if(!ExtractArgs(kParams_SetHotKeySpell, scriptData, opcodeOffsetPtr, thisObj, containingObj, script, locals, &key, &form))
			return true;
	}

	if(form && key >= 1 && key <= kNumQuickKeys)
	{
		BaseExtraList::Locker lock;
		SetQuickKey(key - 1, form);
	}

	return true;
}

static ParamInfo kParams_ModEquippedFloat[2] =
{
	{ "value", kParamType_Float, 0 },
	{ "slot", kParamType_Integer, 0 },
};

static ParamInfo kParams_SetEquippedInt[2] =
{
	{ "value", kParamType_Integer, 0 },
	{ "slot", kParamType_Integer, 0 },
};

static ParamInfo kParams_OneMagicItem[1] =
{
	{ "magic item", kParamType_MagicItem, 0 },
};

CommandInfo kCommandInfo_GetObjectHealth =
{
	"GetObjectHealth", "",
	0,
	"returns the health of the object or calling reference",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetObjectHealth_Execute
};

CommandInfo kCommandInfo_GetEquippedCurrentHealth =
{
	"GetEquippedCurrentHealth", "GetECHealth",
	0,
	"returns the current health of the actor's specified equipment slot",
	1,
	1, kParams_OneInt,
	Cmd_GetEquippedCurrentHealth_Execute
};

CommandInfo kCommandInfo_SetEquippedCurrentHealth =
{
	"SetEquippedCurrentHealth", "SetECHealth",
	0,
	"Sets the health of the item in the specified equipment slot",
	1,
	2, kParams_SetEquippedInt,
	Cmd_SetEquippedCurrentHealth_Execute
};

CommandInfo kCommandInfo_ModEquippedCurrentHealth =
{
	"ModEquippedCurrentHealth", "ModECHealth",
	0,
	"modifies the health of the item in the specified equipment slot",
	1,
	2, kParams_ModEquippedFloat,
	Cmd_ModEquippedCurrentHealth_Execute
};

CommandInfo kCommandInfo_GetCurrentHealth =
{
	"GetCurrentHealth", "",
	0,
	"returns the current health of the reference",
	1,
	0, nullptr,
	Cmd_GetCurrentHealth_Execute
};

CommandInfo kCommandInfo_SetCurrentHealth =
{
	"SetCurrentHealth", "SetCHealth",
	0,
	"sets the current health to the specified value",
	0,
	1, kParams_OneFloat,
	Cmd_SetCurrentHealth_Execute
};

CommandInfo kCommandInfo_SetEquippedCurrentCharge =
{
	"SetEquippedCurrentCharge", "SetECCharge",
	0,
	"Sets the charge of the item in the specified equipment slot",
	1,
	2, kParams_SetEquippedInt,
	Cmd_SetEquippedCurrentCharge_Execute
};

CommandInfo kCommandInfo_GetEquippedWeaponPoison =
{
	"GetEquippedWeaponPoison", "GetEWPoison",
	0,
	"gets the poison on the equipped weapon.",
	1,
	0, nullptr,
	Cmd_GetEquippedWeaponPoison_Execute
};

CommandInfo kCommandInfo_SetEquippedWeaponPoison =
{
	"SetEquippedWeaponPoison", "SetEWPoison",
	0,
	"Sets the poison on the equipped weapon.",
	1,
	1, kParams_OneMagicItem,
	Cmd_SetEquippedWeaponPoison_Execute
};

CommandInfo kCommandInfo_RemoveEquippedWeaponPoison =
{
	"RemoveEquippedWeaponPoison", "RemEWPoison",
	0,
	"removes the poison on the equipped weapon and returns it.",
	1,
	0, nullptr,
	Cmd_RemoveEquippedWeaponPoison_Execute
};

CommandInfo kCommandInfo_GetHotKeyItem =
{
	"GetHotKeyItem", "",
	0,
	"returns the item or spell bound to the specified hotkey",
	0,
	1, kParams_OneInt,
	Cmd_GetHotKeyItem_Execute
};

CommandInfo kCommandInfo_ClearHotKey =
{
	"ClearHotKey", "ClearHotKeyItem",
	0,
	"clears the item or spell associated with the specified hotkey",
	0,
	1, kParams_OneInt,
	Cmd_ClearHotKey_Execute
};

CommandInfo kCommandInfo_SetHotKeyItem =
{
	"SetHotKeyItem", "",
	0,
	"sets the spell or item associated with a hotkey",
	0,
	2, kParams_SetHotKeyItem,
	Cmd_SetHotKeyItem_Execute
};

CommandInfo kCommandInfo_GetObjectCharge =
{
	"GetObjectCharge", "",
	0,
	"returns the enchantment charge of the object or calling reference",
	0,
	1, kParams_OneOptionalInventoryObject,
	Cmd_GetObjectCharge_Execute
};

CommandInfo kCommandInfo_GetEquippedCurrentCharge =
{
	"GetEquippedCurrentCharge", "",
	0,
	"returns the current charge of the actor's specified equipment slot",
	1,
	1, kParams_OneInt,
	Cmd_GetEquippedCurrentCharge_Execute
};

CommandInfo kCommandInfo_ModEquippedCurrentCharge =
{
	"ModEquippedCurrentCharge", "",
	0,
	"modifies the charge of the item in the specified equipment slot",
	1,
	2, kParams_ModEquippedFloat,
	Cmd_ModEquippedCurrentCharge_Execute
};

CommandInfo kCommandInfo_GetCurrentCharge =
{
	"GetCurrentCharge", "",
	0,
	"returns the current charge of the calling reference",
	1,
	0, nullptr,
	Cmd_GetCurrentCharge_Execute
};

CommandInfo kCommandInfo_SetCurrentCharge =
{
	"SetCurrentCharge", "",
	0,
	"sets the current charge of the calling reference",
	1,
	1, kParams_OneFloat,
	Cmd_SetCurrentCharge_Execute
};

CommandInfo kCommandInfo_ModCurrentCharge =
{
	"ModCurrentCharge", "",
	0,
	"modifies the current charge of the calling reference",
	1,
	1, kParams_OneFloat,
	Cmd_ModCurrentCharge_Execute
};

CommandInfo kCommandInfo_GetEquipmentSlotType =
{
	"GetEquipmentSlotType", "gest",
	0,
	"returns a ref to the type of the specified equipment slot",
	1,
	1, kParams_OneInt,
	Cmd_GetEquipmentSlotType_Execute
};

// intentionally the same as GetInventoryItemType
CommandInfo kCommandInfo_GetInventoryObject =
{
	"GetInventoryObject", "GetInvObj",
	0,
	"returns the base object for the item in the specified inventory index",
	1,
	1, kParams_OneInt,
	Cmd_GetInventoryItemType_Execute
};

// intentionally the same as GetEquipmentSlotType
CommandInfo kCommandInfo_GetEquippedObject =
{
	"GetEquippedObject", "GetEqObj",
	0,
	"returns the base object of the item in the specified equipment slot",
	1,
	1, kParams_OneInt,
	Cmd_GetEquipmentSlotType_Execute
};
