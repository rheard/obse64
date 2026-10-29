#pragma once

#include "GameBSExtraData.h"
#include "GameTypes.h"
#include "GameForms.h"

class TESForm;
class TESObjectREFR;

// 20
class ExtraContainerChanges : public BSExtraData
{
public:
	enum { kID = kExtraData_ContainerChanges };

	// 18
	struct Entry
	{
		BSSimpleList <ExtraDataList *>	* extraData;	// 00

		s32	countDelta;	// 08
		u32	pad0C;		// 0C
		TESForm	* type;	// 10
	};

	// 18
	struct Data
	{
		BSSimpleList <Entry *>	* objList;	// 00
		TESObjectREFR			* owner;	// 08

		float	totalWeight;	// 10 of all items in inventory. cached, is -1 if needs to be recalculated
		float	armorWeight;	// 14 weight of equipped armor. cached as above. both take armor weight perks into account
	};

	Data	* data;	// 18

	BSSimpleList <Entry *> * GetObjList() const { return data ? data->objList : nullptr; }
};

static_assert(sizeof(ExtraContainerChanges) == 0x20);
static_assert(sizeof(ExtraContainerChanges::Data) == 0x18);
static_assert(sizeof(ExtraContainerChanges::Entry) == 0x18);

// 20
class ExtraMapMarker : public BSExtraData
{
public:
	enum { kID = kExtraData_MapMarker };

	// 20
	struct Data
	{
		TESFullName	fullName;	// 00
		u8			flags;		// 18
		u8			pad19;		// 19
		u16			type;		// 1A
		u32			pad1C;		// 1C
	};

	Data * data;	// 18
};

static_assert(sizeof(ExtraMapMarker) == 0x20);
static_assert(sizeof(ExtraMapMarker::Data) == 0x20);

// 20
class ExtraOwnership : public BSExtraData
{
public:
	enum { kID = kExtraData_Ownership };

	TESForm	* owner;	// 18 an NPC or a faction
};

static_assert(sizeof(ExtraOwnership) == 0x20);

// 20
// the rank an owning faction requires of its members, only present when it's above 0
class ExtraRank : public BSExtraData
{
public:
	enum { kID = kExtraData_Rank };

	u32	rank;	// 18
	u32	pad1C;	// 1C
};

static_assert(sizeof(ExtraRank) == 0x20);

// 20
// an enchanted item's charge, only present while it isn't full
class ExtraCharge : public BSExtraData
{
public:
	enum { kID = kExtraData_Charge };

	float	charge;	// 18
	u32		pad1C;	// 1C
};

static_assert(sizeof(ExtraCharge) == 0x20);

// 20
// an item's health (its condition), only present while it isn't full; it can be above full, as a
// skilled armorer repairs past 100%
class ExtraHealth : public BSExtraData
{
public:
	enum { kID = kExtraData_Health };

	float	health;	// 18
	u32		pad1C;	// 1C
};

static_assert(sizeof(ExtraHealth) == 0x20);

// 20
// the poison put on a weapon, until it hits
class ExtraPoison : public BSExtraData
{
public:
	enum { kID = kExtraData_Poison };

	TESForm	* poison;	// 18 the poison (an AlchemyItem)
};

static_assert(sizeof(ExtraPoison) == 0x20);

// 20
// how many items in an inventory share an extra data list, only present when it's more than one
class ExtraCount : public BSExtraData
{
public:
	enum { kID = kExtraData_Count };

	s16	count;		// 18
	u8	pad1A[6];	// 1A
};

static_assert(sizeof(ExtraCount) == 0x20);

typedef ExtraCount * (* _ExtraCount_Construct)(void * mem, s16 count);
extern RelocAddr <_ExtraCount_Construct> ExtraCount_Construct;
