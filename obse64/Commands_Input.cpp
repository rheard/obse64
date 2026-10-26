#include "Commands_Classic.h"
#include "GameScript.h"
#include "ParamInfos.h"
#include <Windows.h>

// the original version of the input commands was sketchy and would not work now with a modern input system
// let alone the other weirdness
// so let's document it for now

class InputHook
{
public:
	// Classic read DirectInput's key state, which only changes while the game has focus. The game
	// takes its input from Unreal now, so this asks Windows instead, for the same key, and only
	// while the game's window is in front.
	bool Get(u32 keycode)
	{
		return GetKey(RemapMiceKeycodes(keycode));
	}

	// A key by its own code, without classic's other codes for the mouse buttons (0xFF up), as
	// a look at every key reads them, so a button isn't also seen as 0xFF.
	bool GetKey(u32 keycode)
	{
		u32 virtualKey = VirtualKeyForKeycode(keycode);
		return virtualKey && GameHasFocus() && (GetAsyncKeyState(virtualKey) & 0x8000);
	}
	void Tap(u32 keycode) { }
	void Hold(u32 keycode) { }
	void Release(u32 keycode) { }
	void Hammer(u32 keycode) { }
	void AHammer(u32 keycode) { }
	void UnHammer(u32 keycode) { }
	void Disable(u32 keycode) { }
	void Enable(u32 keycode) { }

	void MouseX(u32 dx) { }
	void MouseY(u32 dy) { }
	void MouseSpeedX(u32 dx) {}
	void MouseSpeedY(u32 dy) {}
	void DisableMouse() { }
	void EnableMouse() { }

private:
	u32 RemapMiceKeycodes(u32 keycode)
	{
		// original was
		// if(keycode%256==255&&keycode<2048) keycode=255+(keycode+1)/256;
		// 
		// this remaps keycodes xFF where x = 0-7 in to 10x (the mice inputs)
		// but only the first 8 bytes (no wheel, buttons only)
		// and I don't remember why but am sure something relies on it

		if(((keycode & 0xFF) == 0xFF) && (keycode <= 0x7FF))
			keycode = 0x100 + (keycode >> 8);

		return keycode;
	}

	// keycodes are DirectInput scan codes, 0x80 set for the keys with an E0 prefix, and from 0x100
	// the mouse buttons
	static u32 VirtualKeyForKeycode(u32 keycode)
	{
		switch(keycode)
		{
			case 0x45:	return VK_NUMLOCK;	// DIK_NUMLOCK and DIK_PAUSE: scan code 45 is both
			case 0xC5:	return VK_PAUSE;
			case 0xB7:	return VK_SNAPSHOT;	// DIK_SYSRQ
			case 0x100:	return VK_LBUTTON;
			case 0x101:	return VK_RBUTTON;
			case 0x102:	return VK_MBUTTON;
			case 0x103:	return VK_XBUTTON1;
			case 0x104:	return VK_XBUTTON2;
		}

		// the number pad's digits share scan codes with the arrows and Home, End, PgUp, PgDn, Ins
		// and Del, which Windows maps them to
		static const u8 kNumpad[] = { VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9, 0, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6, 0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD0, VK_DECIMAL };
		if(keycode >= 0x47 && keycode <= 0x53 && kNumpad[keycode - 0x47])
			return kNumpad[keycode - 0x47];

		if(keycode >= 0x100) return 0;

		u32 scanCode = keycode & 0x7F;
		if(keycode & 0x80)
			scanCode |= 0xE000;

		return MapVirtualKeyExW(scanCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
	}

	static bool GameHasFocus()
	{
		DWORD processID = 0;
		if(HWND window = GetForegroundWindow())
			GetWindowThreadProcessId(window, &processID);

		return processID == GetCurrentProcessId();
	}
};

static InputHook g_InputHook;

bool IsKeycodeDown(u32 keycode)
{
	return g_InputHook.GetKey(keycode);
}

// deprecated

static bool Cmd_IsKeyPressed_Execute(COMMAND_ARGS)
{
	*result = 0;
	u32 keycode = 0;

	if(!ExtractArgs(EXTRACT_ARGS, &keycode)) return true;
	if(GetAsyncKeyState(keycode) & 0x8000) *result = 1;

	return true;
}

static bool Cmd_IsKeyPressed2_Execute(COMMAND_ARGS)
{
	*result = 0;
	u32 keycode = 0;

	if(!ExtractArgs(EXTRACT_ARGS, &keycode)) return true;

	*result = g_InputHook.Get(keycode);

	return true;
}

template <void (InputHook::* Fn)(u32)>
static bool InputHookFn(COMMAND_ARGS)
{
	*result = 0;
	u32 keycode = 0;

	if(!ExtractArgs(EXTRACT_ARGS, &keycode)) return true;

	(g_InputHook.*Fn)(keycode);

	return true;
}

static bool Cmd_DisableMouse_Execute(COMMAND_ARGS)
{
	*result = 0;

	g_InputHook.DisableMouse();

	return true;
}

static bool Cmd_EnableMouse_Execute(COMMAND_ARGS)
{
	*result = 0;

	g_InputHook.EnableMouse();

	return true;
}

CommandInfo kCommandInfo_IsKeyPressed =
{
	"IsKeyPressed", "ikp",
	0,
	"return if a virtual keycode is down or up",
	0,
	1, kParams_OneInt,
	Cmd_IsKeyPressed_Execute
};

CommandInfo kCommandInfo_IsKeyPressed2 =
{
	"IsKeyPressed2", "ikp2",
	0,
	"return if a dx scancode is down or up",
	0,
	1, kParams_OneInt,
	Cmd_IsKeyPressed2_Execute
};

CommandInfo kCommandInfo_TapKey =
{
	"TapKey", "tk",
	0,
	"Fakes a key press for one frame",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Tap>
};

CommandInfo kCommandInfo_HoldKey =
{
	"HoldKey", "hk",
	0,
	"Fakes a key press indefinately",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Hold>
};

CommandInfo kCommandInfo_ReleaseKey =
{
	"ReleaseKey", "rk",
	0,
	"Releases a key held down by HoldKey",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Release>
};

CommandInfo kCommandInfo_HammerKey =
{
	"HammerKey", "hk",	// duplicate. only the first will be found
	0,
	"Fakes key presses in alternate frames",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Hammer>
};

CommandInfo kCommandInfo_AHammerKey =
{
	"AHammerKey", "ahk",
	0,
	"Fakes key presses in alternate frames",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::AHammer>
};

CommandInfo kCommandInfo_UnHammerKey =
{
	"UnHammerKey", "uhk",
	0,
	"Releases a key being hammered by HammerKey or AHammerKey",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::UnHammer>
};

CommandInfo kCommandInfo_DisableKey =
{
	"DisableKey", "dk",
	0,
	"Prevents a player from using a key",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Disable>
};

CommandInfo kCommandInfo_EnableKey =
{
	"EnableKey", "ek",
	0,
	"Reenables a key previously disabled with DisableKey",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::Enable>
};

CommandInfo kCommandInfo_MoveMouseX =
{
	"MoveMouseX", "mmx",
	0,
	"Fakes a mouse movement x pixels along the x axis",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::MouseX>
};

CommandInfo kCommandInfo_MoveMouseY =
{
	"MoveMouseY", "mmy",
	0,
	"Fakes a mouse movement x pixels along the y axis",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::MouseY>
};

CommandInfo kCommandInfo_SetMouseSpeedX =
{
	"SetMouseSpeedX", "smsx",
	0,
	"Moves the mouse x pixels per second along the x axis",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::MouseSpeedX>
};

CommandInfo kCommandInfo_SetMouseSpeedY =
{
	"SetMouseSpeedY", "smsy",
	0,
	"Moves the mouse x pixels per second along the y axis",
	0,
	1, kParams_OneInt,
	InputHookFn<&InputHook::MouseSpeedY>
};

CommandInfo kCommandInfo_DisableMouse =
{
	"DisableMouse", "dm",
	0,
	"Disables mouse x/y axis movement",
	0,
	0, nullptr,
	Cmd_DisableMouse_Execute
};

CommandInfo kCommandInfo_EnableMouse =
{
	"EnableMouse", "em",
	0,
	"Enables the mouse after it has been disabled by DisableMouse",
	0,
	0, nullptr,
	Cmd_EnableMouse_Execute
};
