#include "Commands_Classic.h"
#include "GameScript.h"
#include "ParamInfos.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

// the original version of the input commands was sketchy and would not work now with a modern input system
// let alone the other weirdness
// so let's document it for now

// Unreal reads the keyboard and mouse from the game window's messages, on its game thread, which
// owns the window. A disabled key's key-down messages are dropped before Unreal's window procedure
// sees them (the window is subclassed the first time a key is disabled or pressed for a script),
// and a key that's down as it's disabled is let go. A key a script taps, holds or releases gets the
// messages it would send, posted on the game thread once each Unreal frame; a tap's key-up goes a
// frame after its key-down, so Unreal sees the press. Scripts reading a key (IsKeyPressed2) ask
// Windows, so they still see a disabled key, as classic's did.
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
	void Tap(u32 keycode) { Request(keycode, kRequest_Tap); }
	void Hold(u32 keycode) { Request(keycode, kRequest_Down); }
	void Release(u32 keycode) { Request(keycode, kRequest_Up); }
	void Hammer(u32 keycode) { Request(keycode, kRequest_Hammer); }
	void AHammer(u32 keycode) { Request(keycode, kRequest_AHammer); }
	void UnHammer(u32 keycode) { Request(keycode, kRequest_UnHammer); }
	void Disable(u32 keycode) { Request(keycode, kRequest_Disable); }
	void Enable(u32 keycode) { Request(keycode, kRequest_Enable); }

	void MouseX(u32 dx) { }
	void MouseY(u32 dy) { }
	void MouseSpeedX(u32 dx) {}
	void MouseSpeedY(u32 dy) {}
	void DisableMouse() { }
	void EnableMouse() { }

	// does what scripts asked, on the game thread
	void Update()
	{
		std::vector <KeyRequest> requests;
		{
			std::lock_guard <std::mutex> lock(m_requestLock);
			requests.swap(m_requests);
		}

		if(!requests.empty() && !Subclass()) return;

		if(!m_window) return;

		// last frame's taps come up
		for(u32 keycode : m_tapped)
			PostKey(keycode, false);
		m_tapped.clear();

		for(const auto & request : requests)
		{
			u32 keycode = request.keycode;

			switch(request.type)
			{
				case kRequest_Tap:		PostKey(keycode, true);	m_tapped.push_back(keycode);	break;
				case kRequest_Down:		PostKey(keycode, true);									break;
				case kRequest_Up:		PostKey(keycode, false);								break;
				case kRequest_Hammer:	m_hammered[keycode] = 1;								break;
				case kRequest_AHammer:	m_hammered[keycode] = 2;								break;

				case kRequest_UnHammer:
					if(m_hammered[keycode])
					{
						m_hammered[keycode] = 0;
						PostKey(keycode, false);
					}
					break;

				case kRequest_Disable:
					if(!s_disabled[keycode].exchange(true) && Get(keycode))
						PostKey(keycode, false);
					break;

				case kRequest_Enable:
					s_disabled[keycode] = false;
					break;
			}
		}

		// hammered keys go down and up on alternate frames, AHammered ones on the others
		m_frame ^= 1;
		for(u32 keycode = 0; keycode < kNumKeys; keycode++)
		{
			if(m_hammered[keycode])
				PostKey(keycode, (m_hammered[keycode] == 1) == (m_frame == 0));
		}
	}

private:
	enum
	{
		kNumKeys = 0x105,	// the keyboard's scan codes, then the left, right and middle mouse buttons and two side buttons

		kRequest_Tap = 0,
		kRequest_Down,
		kRequest_Up,
		kRequest_Hammer,
		kRequest_AHammer,
		kRequest_UnHammer,
		kRequest_Disable,
		kRequest_Enable,
	};

	struct KeyRequest
	{
		u32	keycode;
		u32	type;
	};

	void Request(u32 keycode, u32 type)
	{
		keycode = RemapMiceKeycodes(keycode);
		if(keycode >= kNumKeys || !VirtualKeyForKeycode(keycode)) return;

		std::lock_guard <std::mutex> lock(m_requestLock);
		m_requests.push_back({ keycode, type });
	}

	// The key a keyboard or mouse button message is for, or kNumKeys for any other message. A
	// keyboard message gives the scan code in bits 16 to 23 of lParam, with bit 24 set for the
	// keys with an E0 prefix, which is how DirectInput numbered them.
	static u32 KeycodeForMessage(UINT msg, WPARAM wParam, LPARAM lParam, bool * down)
	{
		switch(msg)
		{
			case WM_KEYDOWN: case WM_SYSKEYDOWN:	*down = true;	break;
			case WM_KEYUP: case WM_SYSKEYUP:		*down = false;	break;

			case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:	*down = true;	return 0x100;
			case WM_LBUTTONUP:							*down = false;	return 0x100;
			case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:	*down = true;	return 0x101;
			case WM_RBUTTONUP:							*down = false;	return 0x101;
			case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:	*down = true;	return 0x102;
			case WM_MBUTTONUP:							*down = false;	return 0x102;

			case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
				*down = msg != WM_XBUTTONUP;
				return (HIWORD(wParam) == XBUTTON1) ? 0x103 : 0x104;

			default:
				return kNumKeys;
		}

		return ((lParam >> 16) & 0x7F) | ((lParam & (1 << 24)) ? 0x80 : 0);
	}

	static LRESULT CALLBACK WindowProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		bool down = false;
		u32 keycode = KeycodeForMessage(msg, wParam, lParam, &down);

		// a key-down for a disabled key is dropped, unless a script pressed the key itself
		if(keycode < kNumKeys && down && s_disabled[keycode])
		{
			if(s_posted[keycode] > 0)
				s_posted[keycode]--;
			else
				return 0;
		}

		return CallWindowProcW(s_originalProc, window, msg, wParam, lParam);
	}

	static BOOL CALLBACK FindGameWindow(HWND window, LPARAM out)
	{
		char className[32];
		if(IsWindowVisible(window) && GetClassNameA(window, className, sizeof(className)) && !strcmp(className, "UnrealWindow"))
		{
			*reinterpret_cast <HWND *>(out) = window;
			return FALSE;
		}

		return TRUE;
	}

	// Subclasses the game's window, Unreal's own on this thread, once, and again only if it's been
	// made anew. A window whose procedure isn't ours any more is left alone: whatever subclassed it
	// after us (an overlay, another plugin) passes its messages on to ours, so subclassing it again
	// would make that procedure our original, and the two would pass each message back and forth
	// until the stack ran out.
	bool Subclass()
	{
		if(m_window && IsWindow(m_window))
			return true;

		m_window = nullptr;

		HWND window = nullptr;
		EnumThreadWindows(GetCurrentThreadId(), FindGameWindow, LPARAM(&window));
		if(!window) return false;

		s_originalProc = reinterpret_cast <WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, LONG_PTR(WindowProc)));
		if(!s_originalProc) return false;

		m_window = window;

		return true;
	}

	// posts the message a key or mouse button sends as it goes down or comes up
	void PostKey(u32 keycode, bool down)
	{
		if(keycode >= 0x100)
		{
			POINT cursor = {};
			GetCursorPos(&cursor);
			ScreenToClient(m_window, &cursor);
			LPARAM position = MAKELPARAM(cursor.x, cursor.y);

			static const UINT kDown[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN, WM_XBUTTONDOWN };
			static const UINT kUp[] = { WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP };
			static const WPARAM kButton[] = { MK_LBUTTON, MK_RBUTTON, MK_MBUTTON, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKEWPARAM(MK_XBUTTON2, XBUTTON2) };

			u32 button = keycode - 0x100;
			WPARAM wParam = down ? kButton[button] : (button >= 3 ? MAKEWPARAM(0, HIWORD(kButton[button])) : 0);

			if(down && s_disabled[keycode]) s_posted[keycode]++;
			PostMessageW(m_window, down ? kDown[button] : kUp[button], wParam, position);
			return;
		}

		LPARAM lParam = 1 | (LPARAM(keycode & 0x7F) << 16) | ((keycode & 0x80) ? (1 << 24) : 0);
		if(!down)
			lParam |= (LPARAM(1) << 30) | (LPARAM(1) << 31);

		if(down && s_disabled[keycode]) s_posted[keycode]++;
		PostMessageW(m_window, down ? WM_KEYDOWN : WM_KEYUP, VirtualKeyForKeycode(keycode), lParam);
	}

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

	std::mutex					m_requestLock;
	std::vector <KeyRequest>	m_requests;		// from scripts, for the game thread

	// game thread only
	HWND				m_window = nullptr;
	std::vector <u32>	m_tapped;				// keys tapped last frame, to come up
	u8					m_hammered[kNumKeys] = {};	// 1 hammered, 2 AHammered
	u8					m_frame = 0;

	static std::atomic <bool>	s_disabled[kNumKeys];
	static std::atomic <u32>	s_posted[kNumKeys];	// key-downs posted and not yet seen, for disabled keys
	static WNDPROC				s_originalProc;
};

std::atomic <bool>	InputHook::s_disabled[InputHook::kNumKeys];
std::atomic <u32>	InputHook::s_posted[InputHook::kNumKeys];
WNDPROC				InputHook::s_originalProc = nullptr;

static InputHook g_InputHook;

bool IsKeycodeDown(u32 keycode)
{
	return g_InputHook.GetKey(keycode);
}

void UpdateKeyInput()
{
	g_InputHook.Update();
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

// classic's, for keyboard keys: how many are down, and the index-th of them (from 0) by scan code

static bool Cmd_GetNumKeysPressed_Execute(COMMAND_ARGS)
{
	u32 count = 0;
	for(u32 keycode = 0; keycode < 0x100; keycode++)
	{
		if(g_InputHook.GetKey(keycode))
			count++;
	}

	*result = count;

	return true;
}

static bool Cmd_GetKeyPress_Execute(COMMAND_ARGS)
{
	*result = 0xFFFF;
	u32 index = 0;

	if(!ExtractArgs(EXTRACT_ARGS, &index)) return true;

	for(u32 keycode = 0; keycode < 0x100; keycode++)
	{
		if(g_InputHook.GetKey(keycode) && !index--)
		{
			*result = keycode;
			break;
		}
	}

	return true;
}

CommandInfo kCommandInfo_GetNumKeysPressed =
{
	"GetNumKeysPressed", "",
	0,
	"returns the number of keys pressed",
	0,
	0, nullptr,
	Cmd_GetNumKeysPressed_Execute
};

CommandInfo kCommandInfo_GetKeyPress =
{
	"GetKeyPress", "",
	0,
	"returns the scancode of the nth key pressed",
	0,
	1, kParams_OneInt,
	Cmd_GetKeyPress_Execute
};
