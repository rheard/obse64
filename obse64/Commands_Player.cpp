#include "GameScript.h"
#include "GameObjects.h"
#include "GameConsole.h"
#include "GameRTTI.h"
#include "ParamInfos.h"

class MagicItem;
class SpellItem;

static bool Cmd_GetActiveSpell_Execute(COMMAND_ARGS)
{
	*result = 0;

	// none before a game is under way, as at the main menu's console
	PlayerCharacter * player = PlayerCharacter::Get();
	if(!player) return true;

	MagicItem * activeMagicItem = CALL_MEMBER_FN(player, GetActiveMagicItem)();

	auto * activeMagicItemForm = DYNAMIC_CAST(activeMagicItem, MagicItem, TESForm);
	if(activeMagicItemForm)
		SetFormResult(result, activeMagicItemForm->refID);

	if(IsConsoleMode())
	{
		Console_Print("GetActiveSpell >> %08X (%s)", activeMagicItemForm ? activeMagicItemForm->refID : 0, GetFullName(activeMagicItemForm));
	}

	return true;
}

static bool Cmd_SetActiveSpell_Execute(COMMAND_ARGS)
{
	TESForm * spell = NULL;

	if(!ExtractArgs(EXTRACT_ARGS, &spell)) return true;

	auto * spellItem = DYNAMIC_CAST(spell, TESForm, SpellItem);
	PlayerCharacter * player = PlayerCharacter::Get();
	if(spellItem && player)
	{
		CALL_MEMBER_FN(player, SetActiveSpell)(&spellItem->magicItem);
	}

	return true;
}

CommandInfo kCommandInfo_GetActiveSpell =
{
	"GetPlayerSpell", "GetActiveSpell",
	0,
	"returns the base spell object for the player's active spell",
	0,
	0, nullptr,
	Cmd_GetActiveSpell_Execute
};

CommandInfo kCommandInfo_SetActiveSpell =
{
	"SetActiveSpell", "sspl",
	0,
	"sets the active spell to the argument",
	0,
	1, kParams_OneSpellItem,
	Cmd_SetActiveSpell_Execute
};
