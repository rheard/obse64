#include "GameScript.h"
#include "GameBSExtraData.h"
#include "GameExtraData.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameConsole.h"
#include "ParamInfos.h"
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
	kSlot_None = -1,
	// lower slots are TESBipedModelForm::kPart_*
	kSlot_RightRing = TESBipedModelForm::kPart_RightRing,
	kSlot_LeftRing,
	kSlot_Torch = TESBipedModelForm::kPart_Torch,	// 14
	kSlot_Weapon = TESBipedModelForm::kPart_Max,	// 16
	kSlot_Ammo,
	kSlot_RangedWeapon,
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

static u32 GetItemSlot(TESForm * type)
{
	if(type)
	{
		if(auto * bip = DYNAMIC_CAST(type, TESForm, TESBipedModelForm))
			return bip->GetSlot();
		else if(DYNAMIC_CAST(type, TESForm, TESObjectWEAP))
			return kSlot_Weapon;
		else if(DYNAMIC_CAST(type, TESForm, TESAmmo))
			return kSlot_Ammo;
		else
		{
			TESObjectLIGH * pLight = DYNAMIC_CAST(type, TESForm, TESObjectLIGH);
			if(pLight && pLight->IsCarriable())
				return kSlot_Torch;
		}
	}

	return kSlot_None;
}

static u32 GetItemSlotMask(TESForm * type)
{
	u32	result = 0;

	if(type)
	{
		auto * bip = DYNAMIC_CAST(type, TESForm, TESBipedModelForm);
		auto * weap = DYNAMIC_CAST(type, TESForm, TESObjectWEAP);

		if(bip)
			result = bip->partMask;
		else if(weap)
			result = (weap->type == TESObjectWEAP::kType_Bow) ? (1 << kSlot_RangedWeapon) : (1 << kSlot_Weapon);
		else if(DYNAMIC_CAST(type, TESForm, TESAmmo))
			result = (1 << kSlot_Ammo);
	}

	return result;
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

static inline bool IsOppositeRing(u32 slot, u32 slotIdx)
{
	return (slot == kSlot_RightRing && slotIdx == kSlot_LeftRing) || (slot == kSlot_LeftRing && slotIdx == kSlot_RightRing);
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

static bool FindEquippedByMask(TESObjectREFR * thisObj, u32 targetMask, u32 targetData, FoundEquipped * foundEquippedFunctor, double * result)
{
	auto * containerChanges = thisObj->extraList.Get<ExtraContainerChanges>();
	if(containerChanges && containerChanges->data && containerChanges->data->objList)
	{
		for(auto * entry = &containerChanges->data->objList->node; entry; entry = entry->m_next)
		{
			// do the fast check first (an object must have extend data to be equipped)
			if(entry->m_data && entry->m_data->extraData && entry->m_data->type)
			{
				// does it match the slot?
				u32 slotMask = GetItemSlotMask(entry->m_data->type);
				if((slotMask & targetMask) == targetData)
				{
					for(auto * extend = &entry->m_data->extraData->node; extend; extend = extend->m_next)
					{
						if(extend->m_data)
						{
							// is it equipped?
							bool	isEquipped = false;

							isEquipped = extend->m_data->Contains(kExtraData_Worn);

							// special-case for left ring
							if(!isEquipped)
								isEquipped = (slotMask & (1 << TESBipedModelForm::kPart_LeftRing)) && extend->m_data->Contains(kExtraData_WornLeft);

							if(isEquipped)
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

			//Console_Print("refID = %08X (%s)", entryData->type->refID, GetFullName(entry->data->type));
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

static bool Cmd_GetObjectCharge_Execute(COMMAND_ARGS)
{
	*result = 0;

	TESForm * form = nullptr;
	if(!ExtractArgs(EXTRACT_ARGS, &form)) return true;

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
		auto * xCharge = extendData->m_data->Get<ExtraCharge>();
		*result = xCharge ? xCharge->charge : GetFullCharge(entryData->type);
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
		if(!DYNAMIC_CAST(entryData->type, TESForm, TESEnchantableForm)) return true;

		ExtraDataList * list = extendData->m_data;
		auto * xCharge = list->Get<ExtraCharge>();

		float fullCharge = float(GetFullCharge(entryData->type));
		float charge = (xCharge ? xCharge->charge : fullCharge) + m_amount;
		if(charge < 0)
			charge = 0;

		if(charge == fullCharge)
		{
			if(xCharge)
				BaseExtraList_Remove(list, xCharge, true);
		}
		else if(xCharge)
			xCharge->charge = charge;
		else if(void * mem = GameHeap_Allocate(sizeof(ExtraCharge)))
			BaseExtraList_Add(list, ExtraCharge_Construct(mem, charge));

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

static ParamInfo kParams_ModEquippedFloat[2] =
{
	{ "value", kParamType_Float, 0 },
	{ "slot", kParamType_Integer, 0 },
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
