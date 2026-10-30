#pragma once

#include "GameTypes.h"
#include "obse64_common/Relocation.h"

// The game's menus, and the messages it gives Unreal to show.

// the interface manager (classic's InterfaceManager)
extern RelocPtr <u8 *> g_interfaceManager;

enum
{
	// classic's menu types
	kMenuType_Magic =	1022,
};

// The open menu of a type, none while it's closed. The open menus' tiles are kept by menu type
// (classic's g_TileMenuArray, 0x066F1F80); a tile menu's Menu is at +0x88, and Menu's destructor
// (0x066F84F0) clears both.
u8 * GetMenuByType(u32 menuType);

// one of a tile's values, as a number (classic's Tile::GetValueFloat)
typedef float (* _Tile_GetFloat)(u8 * tile, u32 valueID);
extern RelocAddr <_Tile_GetFloat> Tile_GetFloat;

// The game's messages for Unreal (0x20 bytes) are a list of parts of 0x38 bytes, each a key and
// named arguments. Unreal turns each part's key into text, formats its arguments into it
// (FText::Format: {name}, with ` before a brace that's meant), and joins the parts. A string
// argument that is a key gets translated there, on Unreal's thread, where its string tables can
// be read. 0x0655A5A0 adds a part with one string argument,
// as the game does for a disease's name; 0x04722180 destroys the parts. The game's Message and
// MessageBox show such a message as a notification (0x0656C6B0) and a message box (0x06568C40).
struct UIMessage
{
	u32		unk00;		// 00
	void	* parts[3];	// 08 begin, end, capacity
};

static_assert(sizeof(UIMessage) == 0x20);

typedef void (* _UIMessage_AddPart)(UIMessage * message, const GameStdString * key, const char * argName, const char * argValue);
extern RelocAddr <_UIMessage_AddPart> UIMessage_AddPart;

typedef void (* _UIMessage_DestroyParts)(void ** parts);
extern RelocAddr <_UIMessage_DestroyParts> UIMessage_DestroyParts;

// shows a message as a notification (classic's QueueUIMessage)
typedef bool (* _QueueUIMessage)(UIMessage * message, void * unk, u32 notification, float duration);
extern RelocAddr <_QueueUIMessage> QueueUIMessage;

// shows a message with an icon, as AddItem and RemoveItem say what they added or removed
// (classic's QueueUIMessage_2)
typedef bool (* _QueueUIMessageWithIcon)(void * message, float duration, const char * icon, void * sound, u8 unk);
extern RelocAddr <_QueueUIMessageWithIcon> QueueUIMessageWithIcon;

// the message, the callback that records the button pressed, flags, a button of its own (or
// none), then the buttons, up to an empty one
typedef bool (* _ShowMessageBox)(UIMessage * message, uintptr_t callback, u32 flags, const char * ownButton, ...);
extern RelocAddr <_ShowMessageBox> ShowMessageBox;
