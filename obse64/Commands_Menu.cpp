#include "Commands_Classic.h"
#include "GameConsole.h"
#include "GameForms.h"
#include "GameMenus.h"
#include "GameRTTI.h"
#include "GameScript.h"
#include "ParamInfos.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"

// classic OBSE's commands about the game's menus (Commands_Classic.cpp registers them)

enum
{
	// classic's tile value for what kind of item a menu list's item is
	kTileValue_user7 =	0xFB5,
};

// The magic menu is still classic's MagicMenu under Unreal's. Unreal's handlers for hovering or
// clicking an item (0x0656F470, 0x0656F3B0) call 0x06774440 with the menu and the item's index,
// which finds the item's tile and shows its details. Nothing keeps the index, so OBSE64 notes it
// on the way, with the spell it showed, and forgets it when a new magic menu is built (in the
// factory, 0x065DD280).
//
// The menu's list is a copy made when it opens, rebuilt when an item is clicked or the filter
// changes (0x06775DA0, which sends Unreal the new list); RemoveSpell doesn't touch it. A click
// rebuilds the list before the click handler reports the hover, so the hovered index is read in
// the new list; but after a rebuild with no hover since, the hovered row can hold another spell,
// so the selection is only given while the list still has the same spell at the hovered index.
typedef void (* _MagicMenu_Hover)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_Hover> MagicMenu_Hover(0x06774440);
static RelocAddr <uintptr_t> kMagicMenuHover_HoverCall(0x0656F4C1);
static RelocAddr <uintptr_t> kMagicMenuHover_ClickCall(0x0656F448);

typedef u8 * (* _MagicMenu_Construct)(u8 * menu);
static RelocAddr <_MagicMenu_Construct> MagicMenu_Construct(0x067731D0);
static RelocAddr <uintptr_t> kMagicMenu_ConstructCall(0x065DD337);

typedef u8 * (* _MagicMenu_GetItemTile)(u8 * menu, s32 index);
static RelocAddr <_MagicMenu_GetItemTile> MagicMenu_GetItemTile(0x06774A40);

struct MagicMenuListNode
{
	TESForm				* data;
	MagicMenuListNode	* next;
};

static u8 * s_hoveredMagicMenu = nullptr;
static s32 s_hoveredMagicIndex = 0;
static TESForm * s_hoveredMagicSpell = nullptr;	// only compared, never read
static u32 s_hoveredMagicSpellID = 0;

// An item of the magic menu, read as its details are (0x06775570): an item of kind 8 is a scroll
// and of kind 0x10 an active effect; any other is a spell, the index-th, counting from 1, in the
// menu's list at +0x70.
static MagicMenuListNode * GetMagicMenuSpellNode(u8 * menu, s32 index)
{
	u8 * tile = MagicMenu_GetItemTile(menu, index);
	if(!tile) return nullptr;

	s32 kind = s32(Tile_GetFloat(tile, kTileValue_user7));
	if(kind == 8 || kind == 0x10)
	{
		// classic gave a scroll's book for a scroll; not ported yet
		return nullptr;
	}

	s32 position = 1;
	for(auto * node = (MagicMenuListNode *)(menu + 0x70); node; node = node->next, position++)
	{
		if(position == index)
			return node;
	}

	return nullptr;
}

static void MagicMenu_Hover_Hook(u8 * menu, s32 index)
{
	MagicMenu_Hover(menu, index);

	MagicMenuListNode * node = GetMagicMenuSpellNode(menu, index);
	SpellItem * spell = (node && node->data) ? DYNAMIC_CAST(node->data, TESForm, SpellItem) : nullptr;

	s_hoveredMagicMenu = menu;
	s_hoveredMagicIndex = index;
	s_hoveredMagicSpell = spell ? node->data : nullptr;
	s_hoveredMagicSpellID = spell ? node->data->refID : 0;
}

static u8 * MagicMenu_Construct_Hook(u8 * menu)
{
	s_hoveredMagicMenu = nullptr;

	return MagicMenu_Construct(menu);
}

// The hovered spell's form ID, while the list still holds that spell where it was hovered. The
// spell is compared by its address, not its node, since the first row's node is the menu's own
// whatever spell it holds; and the ID kept from the hover is given, so a spell freed since isn't
// read.
static u32 GetMagicMenuSelection()
{
	u8 * menu = GetMenuByType(kMenuType_Magic);
	if(!menu || menu != s_hoveredMagicMenu || !menu[0xAC] || !s_hoveredMagicSpell) return 0;

	MagicMenuListNode * node = GetMagicMenuSpellNode(menu, s_hoveredMagicIndex);
	if(!node || node->data != s_hoveredMagicSpell) return 0;

	return s_hoveredMagicSpellID;
}

static bool Cmd_GetActiveMenuSelection_Execute(COMMAND_ARGS)
{
	*result = 0;

	// classic's optional menu type; without it, the menu with the mouse or keyboard focus
	u32 menuType = 0xFFFFFFFF;
	if(!ExtractArgs(EXTRACT_ARGS, &menuType)) return true;

	u32 selection = 0;
	if(menuType == 0xFFFFFFFF || menuType == kMenuType_Magic)
	{
		selection = GetMagicMenuSelection();
	}
	else
	{
		static bool s_reported = false;
		if(!s_reported)
		{
			s_reported = true;
			_MESSAGE("GetActiveMenuSelection only knows the magic menu so far (asked for menu %d)", menuType);
		}
	}

	if(selection)
		SetFormResult(result, selection);

	if(IsConsoleMode())
		Console_Print("GetActiveMenuSelection >> %08X (%s)", selection, GetFullName(selection ? LookupFormByID(selection) : nullptr));

	return true;
}

void InstallMagicMenuHooks()
{
	uintptr_t hover = MagicMenu_Hover.getUIntPtr();
	uintptr_t construct = MagicMenu_Construct.getUIntPtr();

	if(!IsCallTo(kMagicMenuHover_HoverCall.getUIntPtr(), hover) ||
		!IsCallTo(kMagicMenuHover_ClickCall.getUIntPtr(), hover) ||
		!IsCallTo(kMagicMenu_ConstructCall.getUIntPtr(), construct))
	{
		_ERROR("the magic menu's calls aren't where expected; GetActiveMenuSelection won't see the magic menu");
		return;
	}

	g_branchTrampoline.write5Call(kMagicMenuHover_HoverCall.getUIntPtr(), uintptr_t(MagicMenu_Hover_Hook));
	g_branchTrampoline.write5Call(kMagicMenuHover_ClickCall.getUIntPtr(), uintptr_t(MagicMenu_Hover_Hook));
	g_branchTrampoline.write5Call(kMagicMenu_ConstructCall.getUIntPtr(), uintptr_t(MagicMenu_Construct_Hook));
}

CommandInfo kCommandInfo_GetActiveMenuSelection =
{
	"GetActiveMenuSelection", "",
	0,
	"returns the selected item in the active menu",
	0,
	1, kParams_OneOptionalInt,
	Cmd_GetActiveMenuSelection_Execute
};
