#include "Commands_Classic.h"
#include "FunctionScripts.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "ObseExpression.h"
#include "obse64_common/Log.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

// Classic OBSE's event handlers (SetEventHandler, RemoveEventHandler), for the two input events
// xOBSE added: OnKeyEvent, for a key or mouse button, and OnControlEvent, for one of the game's
// controls. Each frame the keys or controls are compared with the frame before, and each handler
// whose filters match is called with the key or control and what happened to it: 0 it went down,
// 1 it came up, 2 it's still down. As in classic, handlers stay set when another game is loaded,
// and go when the game closes: mods set them again when GetGameRestarted says so.

enum
{
	kInputEvent_Key,		// a DirectInput scan code, or from 256 a mouse button
	kInputEvent_Control,	// a control, Forward (0) to Grab (0x1C)
	kInputEvent_Count,

	kNumKeys =		0x108,	// the keyboard and eight mouse buttons; classic's wheel (264, 265) isn't sent
	kNumControls =	29,

	kKeyEvent_Down =	0,
	kKeyEvent_Up =		1,
	kKeyEvent_Hold =	2,

	kFilter_Any = -1,
};

static const char * kInputEventNames[kInputEvent_Count] = { "OnKeyEvent", "OnControlEvent" };

struct InputHandler
{
	Script	* script;
	s32		first;	// the key or control, or any
	s32		second;	// what happened to it, or any

	bool operator==(const InputHandler & rhs) const
	{
		return script == rhs.script && first == rhs.first && second == rhs.second;
	}
};

static std::vector <InputHandler>	s_inputHandlers[kInputEvent_Count];
static bool							s_wasDown[kInputEvent_Count][kNumKeys];
static bool							s_polling[kInputEvent_Count];

// The arguments SetEventHandler and RemoveEventHandler take: the event's name, the function
// script, and up to two filters as pairs, "first"::<key or control> and "second"::<what happened>
// (classic also took "ref" and "object" for those). False if the event isn't an input event.
static bool ExtractInputHandler(COMMAND_ARGS, u32 * eventOut, InputHandler * out)
{
	ObseExpression eval(PASS_COMMAND_ARGS);
	if(!eval.ExtractArgs() || eval.NumArgs() < 2) return false;

	const auto & name = eval.Arg(0);
	const auto & function = eval.Arg(1);
	if(name.type != ObseExpression::Value::kType_String || function.type != ObseExpression::Value::kType_Form)
		return false;

	u32 event = 0;
	while(event < kInputEvent_Count && _stricmp(name.string.c_str(), kInputEventNames[event]))
		event++;

	if(event == kInputEvent_Count)
	{
		static std::set <std::string> s_reported;
		if(s_reported.insert(name.string).second)
			_MESSAGE("event handlers for %s aren't supported yet", name.string.c_str());

		return false;
	}

	TESForm * form = function.formID ? LookupFormByID(function.formID) : nullptr;
	Script * functionScript = form ? DYNAMIC_CAST(form, TESForm, Script) : nullptr;
	if(!functionScript) return false;

	out->script = functionScript;
	out->first = kFilter_Any;
	out->second = kFilter_Any;

	for(u32 i = 2; i < eval.NumArgs(); i++)
	{
		const auto & arg = eval.Arg(i);
		if(arg.type != ObseExpression::Value::kType_Pair ||
			arg.pair[0].type != ObseExpression::Value::kType_String ||
			arg.pair[1].type != ObseExpression::Value::kType_Number)
			continue;

		const char * key = arg.pair[0].string.c_str();
		s32 value = s32(arg.pair[1].number);

		if(!_stricmp(key, "first") || !_stricmp(key, "ref"))
			out->first = value;
		else if(!_stricmp(key, "second") || !_stricmp(key, "object"))
			out->second = value;
	}

	*eventOut = event;

	return true;
}

// 1 if the handler was set, 0 if it was already
static bool Cmd_SetEventHandler_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 event;
	InputHandler handler;
	if(!ExtractInputHandler(PASS_COMMAND_ARGS, &event, &handler)) return true;

	auto & handlers = s_inputHandlers[event];
	if(std::find(handlers.begin(), handlers.end(), handler) == handlers.end())
	{
		handlers.push_back(handler);
		*result = 1;
	}

	return true;
}

// removes the event's handlers for the script that match the filters given; 1 if there were any
static bool Cmd_RemoveEventHandler_Execute(COMMAND_ARGS)
{
	*result = 0;

	u32 event;
	InputHandler match;
	if(!ExtractInputHandler(PASS_COMMAND_ARGS, &event, &match)) return true;

	auto & handlers = s_inputHandlers[event];
	auto removed = std::remove_if(handlers.begin(), handlers.end(), [&match](const InputHandler & handler)
	{
		return handler.script == match.script &&
			(match.first == kFilter_Any || match.first == handler.first) &&
			(match.second == kFilter_Any || match.second == handler.second);
	});

	if(removed != handlers.end())
	{
		handlers.erase(removed, handlers.end());
		*result = 1;
	}

	return true;
}

static void SendInputEvent(u32 event, s32 code, s32 action)
{
	auto & handlers = s_inputHandlers[event];

	std::vector <InputHandler> matching;
	for(const auto & handler : handlers)
	{
		if((handler.first == kFilter_Any || handler.first == code) && (handler.second == kFilter_Any || handler.second == action))
			matching.push_back(handler);
	}

	std::vector <ObseExpression::Value> args(2);
	args[0].type = args[1].type = ObseExpression::Value::kType_Number;
	args[0].number = code;
	args[1].number = action;

	for(const auto & handler : matching)
	{
		// a handler can remove another before its turn
		if(std::find(handlers.begin(), handlers.end(), handler) == handlers.end()) continue;

		CallUserFunction(handler.script, nullptr, nullptr, args);
	}
}

void ProcessInputEvents()
{
	// the handlers' scripts expect a game in progress, with the player in the world
	PlayerCharacter * player = PlayerCharacter::Get();
	bool inGame = player && player->parentCell;

	for(u32 event = 0; event < kInputEvent_Count; event++)
	{
		if(!inGame || s_inputHandlers[event].empty())
		{
			s_polling[event] = false;
			continue;
		}

		// the first frame only learns what's down already
		bool starting = !s_polling[event];
		s_polling[event] = true;

		u32 count = (event == kInputEvent_Key) ? kNumKeys : kNumControls;
		for(u32 code = 0; code < count; code++)
		{
			bool down = (event == kInputEvent_Key) ? IsKeycodeDown(code) : IsControlDown(code);
			bool wasDown = s_wasDown[event][code];
			s_wasDown[event][code] = down;

			if(starting || (!down && !wasDown)) continue;

			SendInputEvent(event, s32(code), down ? (wasDown ? kKeyEvent_Hold : kKeyEvent_Down) : kKeyEvent_Up);
		}
	}
}

CommandInfo kCommandInfo_SetEventHandler =
{
	"SetEventHandler", "",
	0,
	"defines a function script to serve as a callback for game events",
	0,
	0, nullptr,
	Cmd_SetEventHandler_Execute,
	Cmd_Expression_Parse
};

CommandInfo kCommandInfo_RemoveEventHandler =
{
	"RemoveEventHandler", "",
	0,
	"removes event handlers matching the event, script, and optional filters specified",
	0,
	0, nullptr,
	Cmd_RemoveEventHandler_Execute,
	Cmd_Expression_Parse
};
