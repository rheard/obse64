#include "GameScript.h"
#include "GameObjects.h"
#include "GameConsole.h"
#include "GameRTTI.h"
#include "Hooks_Script.h"
#include "ParamInfos.h"
#include "PlayerControls.h"
#include "obse64_common/Log.h"

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

// The game's IsRunning reads an actor's movement flags. Classic set the player's running flag
// whenever Always Run was on (or Run held), moving or not, and mods ask IsRunning whether the
// player runs: No Backwards Running does, to know whether to switch to walking as the player backs
// up. The Remastered's flags for the player come from Unreal, and say running only while moving at
// a run. So for the player on foot, IsRunning in scripts answers as classic did, from Unreal's walk
// toggle and sprinting (PlayerControls.cpp); other actors, the player on a horse, and the game's
// conditions get the game's answer.
enum
{
	kOpcode_IsRunning =	0x111F,
};

static ExecuteFunction s_isRunning = nullptr;

static bool Cmd_IsRunning_Execute(COMMAND_ARGS)
{
	PlayerCharacter * player = PlayerCharacter::Get();
	bool running = false;

	if(thisObj && thisObj == player && !player->horseOrRider && PlayerControls_GetRunning(&running))
	{
		*result = running ? 1 : 0;

		if(IsConsoleMode())
			Console_Print(running ? "%s is running" : "%s is not running", GetFullName(player));

		return true;
	}

	return s_isRunning(PASS_COMMAND_ARGS);
}

void InstallIsRunning()
{
	CommandInfo * info = GetCommandInfo(kOpcode_IsRunning);
	if(!info || !info->execute)
	{
		_ERROR("IsRunning isn't where expected; it keeps the game's answer for the player");
		return;
	}

	s_isRunning = info->execute;
	info->execute = Cmd_IsRunning_Execute;
}
