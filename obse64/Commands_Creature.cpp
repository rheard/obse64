#include "Commands_Classic.h"
#include "GameConsole.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "GameScript.h"

// classic OBSE's commands about creatures (Commands_Classic.cpp registers them)

static ParamInfo kParams_OneOptionalActorBase[1] =
{
	{	"actor base", kParamType_ActorBase, 1 },
};

static bool Cmd_IsCreature_Execute(COMMAND_ARGS)
{
	return IsFormType_Execute(PASS_COMMAND_ARGS, kFormType_Creature, "IsCreature", false);
}

CommandInfo kCommandInfo_IsCreature =
{
	"IsCreature", "",
	0,
	"returns 1 if the passed actor base is a creature",
	0,
	1, kParams_OneOptionalActorBase,
	Cmd_IsCreature_Execute
};

static bool Cmd_GetRider_Execute(COMMAND_ARGS)
{
	*result = 0;

	auto * horse = thisObj ? (Actor *)Runtime_DynamicCast(thisObj, RTTI_TESObjectREFR, RTTI_Creature) : nullptr;
	Actor * rider = horse ? horse->horseOrRider : nullptr;
	if(rider)
		SetFormResult(result, rider->refID);

	if(IsConsoleMode())
		Console_Print("GetRider >> %08X", rider ? rider->refID : 0);

	return true;
}

CommandInfo kCommandInfo_GetRider =
{
	"GetRider", "",
	0,
	"returns a reference to the actor currently riding the calling horse",
	1,
	0, nullptr,
	Cmd_GetRider_Execute
};
