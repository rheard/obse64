#include "PlayerControls.h"
#include "Commands_Classic.h"
#include "UnrealObject.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include "obse64_common/Relocation.h"
#include <atomic>

// Unreal binds each of these controls to UFunctions on the player controller (Altar's
// VEnhancedAltarPlayerController), called through their exec thunks: a pressed and a released
// handler for each way to move, which set how far the player moves that way, and toggles for
// sprinting and walking. The thunks' calls to the handlers go through hooks here, which skip the
// pressed handler or toggle of a disabled control, and note which ways to move are held and when
// the walk toggle is pressed, whether disabled or not, as classic's IsControlPressed read the key
// itself. Once each Unreal frame, the controller is found as UGameplayStatics::GetPlayerController
// finds it, what's down is published for the Oblivion thread, and taps asked for there are made.

enum
{
	kControl_Forward =		0,
	kControl_Back =			1,
	kControl_SlideLeft =	2,
	kControl_SlideRight =	3,
	kControl_Run =			10,
	kControl_AlwaysRun =	11,

	// the player controller: how far the player is moving forward, back, left and right (in
	// classic's order), and whether the sprint and walk toggles are on
	kControllerOffset_Movement =	0xD4C,
	kControllerOffset_Sprinting =	0xA58,
	kControllerOffset_Walking =		0xA59,

	// what s_running holds
	kRunning_Unknown =	0,
	kRunning_No,
	kRunning_Yes,

	// how GetPlayerController (0x031ABE90) gets there: the world's game instance, its local players,
	// and the first one's controller
	kWorldOffset_GameInstance =			0x1B8,
	kGameInstanceOffset_LocalPlayers =	0x38,	// TArray <ULocalPlayer *>
	kLocalPlayerOffset_Controller =		0x30,
};

// the controls these hooks handle
static const u32 kHandledControls = (1 << kControl_Forward) | (1 << kControl_Back) | (1 << kControl_SlideLeft) |
	(1 << kControl_SlideRight) | (1 << kControl_Run) | (1 << kControl_AlwaysRun);

// GWorld, which UEngine::GetWorldFromContextObject (0x037E51E0) falls back on, and the player
// controller's class, as its StaticClass (0x0463F210) keeps it
static RelocPtr <u8 *> g_world(0x092812B8);
static RelocPtr <UClass *> g_altarPlayerControllerClass(0x092E1708);

typedef void (* _InputHandler)(UObject * controller, const double * value);

// each way to move, in classic's order: the exec thunk's call to its pressed handler, and the
// handler, and the released thunk's jump to its handler (its last instruction), and the handler
struct MovementHandler
{
	uintptr_t	pressedCall;
	uintptr_t	pressed;
	uintptr_t	releasedJump;
	uintptr_t	released;
};

static const MovementHandler kMovementHandlers[4] =
{
	{ 0x04640C68, 0x0488AC80, 0x04640C95, 0x0488AD60 },	// MovementForwardInput_Pressed, _Released
	{ 0x04640B98, 0x0488AB80, 0x04640BC5, 0x0488AC70 },	// MovementBackwardInput_
	{ 0x04640D38, 0x0488AD70, 0x04640D65, 0x0488AE60 },	// MovementLeftInput_
	{ 0x04640E08, 0x0488AE70, 0x04640E35, 0x0488AF50 },	// MovementRightInput_
};

static const u32 kMovementControls = (1 << kControl_Forward) | (1 << kControl_Back) | (1 << kControl_SlideLeft) |
	(1 << kControl_SlideRight);

// the sprint toggle (classic's Run) and the walk toggle (Always Run), and their thunks' calls; the
// walk toggle does nothing while the game is set to take both controls from Unreal itself
static RelocAddr <_InputHandler> ToggleSprint(0x0489B9C0);
static RelocAddr <_InputHandler> ToggleWalk(0x0489C0A0);
static RelocAddr <uintptr_t> kToggleSprint_Call(0x04641A68);
static RelocAddr <uintptr_t> kToggleWalk_Call(0x04641B18);

static std::atomic <u32>	s_down = 0;		// controls down, as bits, for the Oblivion thread
static std::atomic <u32>	s_disabled = 0;	// controls disabled, as bits
static std::atomic <u32>	s_held = 0;		// ways to move pressed and not yet released, disabled or not
static std::atomic <u32>	s_walkTaps = 0;	// Always Run taps asked for
static std::atomic <u32>	s_walkPresses = 0;	// Always Run presses since the Oblivion thread last looked
static std::atomic <u32>	s_running = kRunning_Unknown;	// whether the player is set to run

static bool	s_walkDown = false;	// Oblivion thread only: Always Run was pressed for this pass of its loop

static uintptr_t Address(uintptr_t rva)
{
	return RelocationManager::s_baseAddr + rva;
}

static bool IsA(const UObject * object, const UClass * type)
{
	if(!type) return false;

	for(const UStruct * cls = object->m_type; cls; cls = cls->m_parent)
	{
		if(cls == type) return true;
	}

	return false;
}

// the player controller, if there is one and it's Altar's (game thread only)
static u8 * GetPlayerController()
{
	u8 * world = *g_world;
	u8 * gameInstance = world ? *(u8 **)(world + kWorldOffset_GameInstance) : nullptr;
	if(!gameInstance) return nullptr;

	const auto & players = *(const TArray <u8 *> *)(gameInstance + kGameInstanceOffset_LocalPlayers);
	u8 * player = (players.m_data && players.m_len) ? players.m_data[0] : nullptr;
	auto * controller = player ? *(UObject **)(player + kLocalPlayerOffset_Controller) : nullptr;

	return (controller && IsA(controller, *g_altarPlayerControllerClass)) ? reinterpret_cast <u8 *>(controller) : nullptr;
}

template <u32 kControl>
static void MovementPressed_Hook(UObject * controller, const double * value)
{
	s_held |= 1 << kControl;

	if(!(s_disabled & (1 << kControl)))
		reinterpret_cast <_InputHandler>(Address(kMovementHandlers[kControl].pressed))(controller, value);
}

// entered by the released thunk's jump, so it returns to the thunk's caller
template <u32 kControl>
static void MovementReleased_Hook(UObject * controller, const double * value)
{
	s_held &= ~(1 << kControl);

	reinterpret_cast <_InputHandler>(Address(kMovementHandlers[kControl].released))(controller, value);
}

static void ToggleSprint_Hook(UObject * controller, const double * value)
{
	// a disabled Run can't start sprinting, but can still stop it
	if((s_disabled & (1 << kControl_Run)) && controller && !((const u8 *)controller)[kControllerOffset_Sprinting])
		return;

	ToggleSprint(controller, value);
}

// The walk toggle is pressed and done: Unreal sends nothing as it's let go. So a press is counted,
// for Always Run to read down for one pass of the Oblivion loop, disabled or not.
static void ToggleWalk_Hook(UObject * controller, const double * value)
{
	s_walkPresses++;

	if(s_disabled & (1 << kControl_AlwaysRun)) return;

	ToggleWalk(controller, value);
}

static const _InputHandler kPressedHooks[4] =
{
	MovementPressed_Hook <kControl_Forward>, MovementPressed_Hook <kControl_Back>,
	MovementPressed_Hook <kControl_SlideLeft>, MovementPressed_Hook <kControl_SlideRight>,
};

static const _InputHandler kReleasedHooks[4] =
{
	MovementReleased_Hook <kControl_Forward>, MovementReleased_Hook <kControl_Back>,
	MovementReleased_Hook <kControl_SlideLeft>, MovementReleased_Hook <kControl_SlideRight>,
};

bool PlayerControls_GetDown(u32 control, bool * down)
{
	if(control >= 32 || !(kHandledControls & (1 << control))) return false;

	if(control == kControl_AlwaysRun)
		*down = s_walkDown;
	else
		*down = (s_down & (1 << control)) != 0;

	return true;
}

void PlayerControls_BeginOblivionFrame()
{
	s_walkDown = s_walkPresses.exchange(0) != 0;
}

bool PlayerControls_SetDisabled(u32 control, bool disabled)
{
	if(control >= 32 || !(kHandledControls & (1 << control))) return false;

	if(disabled)
		s_disabled |= 1 << control;
	else
		s_disabled &= ~(1 << control);

	return true;
}

bool PlayerControls_GetRunning(bool * running)
{
	u32 state = s_running;
	if(state == kRunning_Unknown) return false;

	*running = state == kRunning_Yes;

	return true;
}

bool PlayerControls_Tap(u32 control)
{
	if(control != kControl_AlwaysRun) return false;

	s_walkTaps++;

	return true;
}

void PlayerControls_Update()
{
	// A new controller (fast travel makes one) hasn't seen the old one's presses, nor will it send
	// their releases. With no controller, nothing is down.
	static u8 * s_controller = nullptr;

	u8 * controller = GetPlayerController();
	if(controller != s_controller)
	{
		s_controller = controller;
		s_held = 0;
	}

	if(!controller)
	{
		s_down = 0;
		s_walkTaps = 0;
		s_walkPresses = 0;
		s_running = kRunning_Unknown;
		return;
	}

	u32 disabled = s_disabled;

	// a disabled way to move is down while its key is held, as classic read a disabled key's own
	// state for IsControlPressed
	u32 down = s_held & disabled & kMovementControls;

	for(u32 control = kControl_Forward; control <= kControl_SlideRight; control++)
	{
		float * amount = reinterpret_cast <float *>(controller + kControllerOffset_Movement + control * sizeof(float));

		// a disabled way to move stops at once, as a disabled key read as up in classic
		if(disabled & (1 << control))
			*amount = 0;

		if(*amount != 0)
			down |= 1 << control;
	}

	if(controller[kControllerOffset_Sprinting])
		down |= 1 << kControl_Run;

	s_down = down;
	s_running = (!controller[kControllerOffset_Walking] || controller[kControllerOffset_Sprinting]) ? kRunning_Yes : kRunning_No;

	// an input action's value, a vector, as a key press gives it
	static const double kPressed[3] = { 1, 0, 0 };

	for(u32 taps = s_walkTaps.exchange(0); taps; taps--)
		ToggleWalk(reinterpret_cast <UObject *>(controller), kPressed);
}

void PlayerControls_Install()
{
	bool found = IsCallTo(kToggleSprint_Call.getUIntPtr(), ToggleSprint.getUIntPtr()) &&
		IsCallTo(kToggleWalk_Call.getUIntPtr(), ToggleWalk.getUIntPtr());

	for(const auto & handler : kMovementHandlers)
	{
		found = found && IsCallTo(Address(handler.pressedCall), Address(handler.pressed)) &&
			IsJumpTo(Address(handler.releasedJump), Address(handler.released));
	}

	if(!found)
	{
		_ERROR("the player's movement and run handlers aren't where expected; the movement, Run and Always Run controls can't be disabled or tapped");
		return;
	}

	g_branchTrampoline.write5Call(kToggleSprint_Call.getUIntPtr(), uintptr_t(ToggleSprint_Hook));
	g_branchTrampoline.write5Call(kToggleWalk_Call.getUIntPtr(), uintptr_t(ToggleWalk_Hook));

	for(u32 i = 0; i < 4; i++)
	{
		g_branchTrampoline.write5Call(Address(kMovementHandlers[i].pressedCall), uintptr_t(kPressedHooks[i]));
		g_branchTrampoline.write5Branch(Address(kMovementHandlers[i].releasedJump), uintptr_t(kReleasedHooks[i]));
	}
}
