#include "GameMenus.h"

RelocPtr <u8 *> g_interfaceManager(0x0941F808);

RelocAddr <_Tile_GetFloat> Tile_GetFloat(0x066F1EE0);

RelocAddr <_UIMessage_AddPart> UIMessage_AddPart(0x0655A5A0);
RelocAddr <_UIMessage_DestroyParts> UIMessage_DestroyParts(0x04722180);
RelocAddr <_QueueUIMessage> QueueUIMessage(0x0656C6B0);
RelocAddr <_QueueUIMessageWithIcon> QueueUIMessageWithIcon(0x0656C3E0);
RelocAddr <_ShowMessageBox> ShowMessageBox(0x06568C40);

typedef u8 * (* _GetTileMenuByType)(u32 menuType);
static RelocAddr <_GetTileMenuByType> GetTileMenuByType(0x066F1F80);

u8 * GetMenuByType(u32 menuType)
{
	u8 * tileMenu = GetTileMenuByType(menuType);
	return tileMenu ? *(u8 **)(tileMenu + 0x88) : nullptr;
}
