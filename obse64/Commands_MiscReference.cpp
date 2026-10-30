#include "GameScript.h"
#include "ParamInfos.h"
#include "Commands_Classic.h"
#include "GameBSExtraData.h"
#include "GameExtraData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameConsole.h"
#include "obse64_common/Log.h"
#include <map>
#include <vector>

static bool Cmd_GetParentCell_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!thisObj) return true;

	auto * currentCell = DYNAMIC_CAST(thisObj->parentCell, TESObjectCELL, TESForm);

	if(currentCell) {
		if(IsConsoleMode()) Console_Print("Cell: %08x", currentCell->refID);
		SetFormResult(result, currentCell->refID);
	}

	return true;
}

CommandInfo kCommandInfo_GetParentCell =
{
	"GetParentCell", "gcel",
	0,
	"returns a ref to the cell the object is in",
	1,
	0, nullptr,
	Cmd_GetParentCell_Execute
};

// classic OBSE's commands about a reference: its owner and whether it's off limits, and the
// references around the player (Commands_Classic.cpp registers them)

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

CommandInfo kCommandInfo_GetOwner =
{
	"GetOwner", "",
	0,
	"returns the owner of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwner_Execute
};

CommandInfo kCommandInfo_GetParentCellOwner =
{
	"GetParentCellOwner", "",
	0,
	"returns the owner of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwner_Execute
};

CommandInfo kCommandInfo_GetOwningFactionRequiredRank =
{
	"GetOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference",
	1,
	0, nullptr,
	Cmd_GetOwningFactionRequiredRank_Execute
};

CommandInfo kCommandInfo_GetParentCellOwningFactionRequiredRank =
{
	"GetParentCellOwningFactionRequiredRank", "",
	0,
	"returns the required rank for ownership of the calling reference's cell",
	1,
	0, nullptr,
	Cmd_GetParentCellOwningFactionRequiredRank_Execute
};

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

static ParamInfo kParams_OneOptionalNPC[1] =
{
	{	"npc", kParamType_NPC, 1 },
};

CommandInfo kCommandInfo_IsOffLimits =
{
	"IsOffLimits", "",
	0,
	"returns true if activating the calling reference would result in a crime for the actor",
	1,
	1, kParams_OneOptionalNPC,
	Cmd_IsOffLimits_Execute
};


// GetFirstRef and GetNextRef walk the references in the player's cell, and in an exterior the
// cells around it to a given depth, one matching reference per call, as classic did. A cell keeps
// its references in the list at +0xB8. An exterior cell (flag 1 of +0x79 clear) has its grid
// coordinates behind the pointer at +0xA0, and its worldspace finds a neighbour by them
// (0x06720A00, which only looks in the worldspace's map of cells, so a cell the game hasn't made
// yet is skipped, not made).
//
// The scan reads a cell's list once, as it comes to the cell, and keeps its references' form IDs
// rather than a place in the list, since the list's nodes don't stay put. A reference leaving the
// cell (0x06660170) frees its node, or for the cell's first reference copies the second into the
// first node, which the cell holds, and frees the second; and scripts move the references they're
// given, or go on with a scan in a later frame. Classic kept the node of the reference it gave
// last, and OBSE64 the node after it, and either could be freed before the next call. Each
// reference is now looked up when its turn comes, and passed over if it's gone or has left the
// cell. One that comes into the cell after the scan has read it isn't given, as a walk never gave
// one: the game adds a reference to the front of the list (0x06655BE0, with 0x06579740), where the
// walk has been.
typedef TESObjectCELL * (* _TESWorldSpace_LookupCell)(TESWorldSpace * world, s32 x, s32 y);
static RelocAddr <_TESWorldSpace_LookupCell> TESWorldSpace_LookupCell(0x06720A00);

enum
{
	// classic's form types for GetFirstRef beyond the game's own
	kRefScanType_Any =			0,
	kRefScanType_Actor =		69,
	kRefScanType_Item =			70,
	kRefScanType_Projectile =	71,
	kRefScanType_MapMarker =	72,
	kRefScanType_Deleted =		90,

	kCellFlag0_Interior =	0x01,

	kRefFlag_Unk02 =	0x02,	// set, along with deleted, on an item picked up from the world
	kRefFlag_Deleted =	0x20,

	kFormID_PlayerBase =	0x07,
	kFormID_MapMarker =		0x10,
};

struct CellRefScan
{
	TESWorldSpace		* world;		// set for an exterior scan of the cells around the player's
	s32					centerX;
	s32					centerY;
	s32					depth;
	s32					dx;				// the cell being walked, from the centre
	s32					dy;
	TESObjectCELL		* cell;			// the cell being walked, none when the scan is done
	std::vector <u32>	refIDs;			// its references when the scan came to it
	size_t				next;			// the next of them to look at
	u32					formType;
	bool				includeTaken;
	bool				includeDeleted;
	u32					gameLoadCount;	// a scan started before a load would look in cells that are gone
};

// one scan per script, as GetNextRef takes no arguments
static std::map <u32, CellRefScan> s_cellRefScans;

static bool RefMatchesScan(const CellRefScan & scan, TESObjectREFR * refr)
{
	TESForm * base = refr->baseForm;
	if(!base) return false;

	bool deleted = (refr->flags & kRefFlag_Deleted) != 0;
	bool taken = (refr->flags & (kRefFlag_Deleted | kRefFlag_Unk02)) == (kRefFlag_Deleted | kRefFlag_Unk02);

	switch(scan.formType)
	{
		case kRefScanType_Any:
			return scan.includeTaken || !taken;

		case kRefScanType_Actor:
			if(deleted && !scan.includeDeleted) return false;

			return (base->typeID == kFormType_Creature) ||
				(base->typeID == kFormType_NPC && base->refID != kFormID_PlayerBase);

		case kRefScanType_Item:
			if(deleted && !scan.includeDeleted) return false;
			if(taken && !scan.includeTaken) return false;

			switch(base->typeID)
			{
				case kFormType_Apparatus:
				case kFormType_Armor:
				case kFormType_Book:
				case kFormType_Clothing:
				case kFormType_Ingredient:
				case kFormType_Misc:
				case kFormType_Weapon:
				case kFormType_Ammo:
				case kFormType_SoulGem:
				case kFormType_Key:
				case kFormType_AlchemyItem:
				case kFormType_SigilStone:
					return true;

				case kFormType_Light:
				{
					auto * light = DYNAMIC_CAST(base, TESForm, TESObjectLIGH);
					return light && light->IsCarriable();
				}
			}

			return false;

		case kRefScanType_Projectile:
			// classic only finds projectiles for the commands that name their shooter
			return false;

		case kRefScanType_MapMarker:
			return (!deleted || scan.includeDeleted) && base->refID == kFormID_MapMarker;

		case kRefScanType_Deleted:
			return deleted;

		default:
			if(deleted && !scan.includeDeleted) return false;
			if(taken && !scan.includeTaken) return false;

			return base->typeID == scan.formType && base->refID != kFormID_PlayerBase;
	}
}

static void WalkCell(CellRefScan & scan, TESObjectCELL * cell)
{
	scan.cell = cell;
	scan.refIDs.clear();
	scan.next = 0;

	for(auto * node = &cell->objectList.node; node; node = node->m_next)
	{
		if(node->m_data && node->m_data->refID)
			scan.refIDs.push_back(node->m_data->refID);
	}
}

// on to the next cell of the square around the centre that the game has made, row by row
static void NextScanCell(CellRefScan & scan)
{
	scan.cell = nullptr;
	scan.refIDs.clear();
	scan.next = 0;

	if(!scan.world) return;

	for(;;)
	{
		if(++scan.dx > scan.depth)
		{
			scan.dx = -scan.depth;
			if(++scan.dy > scan.depth) return;
		}

		if(TESObjectCELL * cell = TESWorldSpace_LookupCell(scan.world, scan.centerX + scan.dx, scan.centerY + scan.dy))
		{
			WalkCell(scan, cell);
			return;
		}
	}
}

static void StartCellRefScan(CellRefScan & scan, TESObjectCELL * cell, u32 formType, u32 depth, bool includeTaken, bool includeDeleted)
{
	scan = CellRefScan();
	scan.formType = formType;
	scan.includeTaken = includeTaken;
	scan.includeDeleted = includeDeleted;
	scan.gameLoadCount = GetGameLoadCount();

	if(!(cell->flags0 & kCellFlag0_Interior) && cell->worldSpace && cell->coords && depth)
	{
		scan.world = cell->worldSpace;
		scan.centerX = cell->coords->x;
		scan.centerY = cell->coords->y;
		scan.depth = s32(depth > 0xFF ? 0xFF : depth);	// classic kept the depth in a byte
		scan.dx = -scan.depth - 1;
		scan.dy = -scan.depth;

		NextScanCell(scan);
	}
	else
		WalkCell(scan, cell);
}

// the next matching reference still in the cell the scan found it in, none once the scan is done
static TESObjectREFR * ContinueCellRefScan(CellRefScan & scan)
{
	while(scan.cell)
	{
		while(scan.next < scan.refIDs.size())
		{
			TESForm * form = LookupFormByID(scan.refIDs[scan.next++]);
			auto * refr = form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr;

			if(refr && refr->parentCell == scan.cell && RefMatchesScan(scan, refr))
				return refr;
		}

		NextScanCell(scan);
	}

	return nullptr;
}

static ParamInfo kParams_GetFirstRef[4] =
{
	{	"form type",			kParamType_Integer,	1 },
	{	"cell depth",			kParamType_Integer,	1 },
	{	"include taken refs",	kParamType_Integer,	1 },
	{	"include deleted refs",	kParamType_Integer,	1 },
};

static bool Cmd_GetFirstRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 formType = 0;
	u32 depth = 0;
	u32 includeTaken = 0;
	u32 includeDeleted = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &formType, &depth, &includeTaken, &includeDeleted)) return true;

	// classic's depth when none is given, so a script giving it means the player's cell alone
	if(depth == 0xFFFFFFFF)
		depth = 0;

	PlayerCharacter * player = PlayerCharacter::Get();
	if(!script || !player || !player->parentCell) return true;

	// Script derives from TESForm
	u32 scriptID = reinterpret_cast <TESForm *>(script)->refID;

	CellRefScan & scan = s_cellRefScans[scriptID];
	StartCellRefScan(scan, player->parentCell, formType, depth, includeTaken != 0, includeDeleted != 0);

	TESObjectREFR * refr = ContinueCellRefScan(scan);
	if(refr)
		SetFormResult(result, refr->refID);
	else
		s_cellRefScans.erase(scriptID);

	if(IsConsoleMode())
		Console_Print("GetFirstRef >> %08X", refr ? refr->refID : 0);

	return true;
}

static bool Cmd_GetNextRef_Execute(COMMAND_ARGS)
{
	*result = 0;

	if(!script) return true;

	// Script derives from TESForm
	auto scan = s_cellRefScans.find(reinterpret_cast <TESForm *>(script)->refID);
	if(scan == s_cellRefScans.end()) return true;

	TESObjectREFR * refr = (scan->second.gameLoadCount == GetGameLoadCount()) ? ContinueCellRefScan(scan->second) : nullptr;
	if(refr)
		SetFormResult(result, refr->refID);
	else
		s_cellRefScans.erase(scan);

	if(IsConsoleMode())
		Console_Print("GetNextRef >> %08X", refr ? refr->refID : 0);

	return true;
}

CommandInfo kCommandInfo_GetFirstRef =
{
	"GetFirstRef", "",
	0,
	"returns the first reference of the specified type in the current cell",
	0,
	4, kParams_GetFirstRef,
	Cmd_GetFirstRef_Execute
};

CommandInfo kCommandInfo_GetNextRef =
{
	"GetNextRef", "",
	0,
	"returns the next reference of a given type in the current cell",
	0,
	0, nullptr,
	Cmd_GetNextRef_Execute
};
