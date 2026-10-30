#include "UnrealTypes.h"
#include <intrin.h>

RelocPtr <FNameStorage> g_fnameStorage(0x0906C780);

FNameEntry * FNameStorage::Get(u32 idx)
{
	u32	entry = (idx >> 16) & 0xFFFF;	// chunk offset
	u32 offset = (idx & 0xFFFF) * 2;	// byte offset

	u8 * buf = (u8 *)m_chunks[entry];

	return (FNameEntry *)(buf + offset);
}

FNameEntry * FNameStorage::Get(const FName & fname)
{
	return Get(fname.m_idx);
}

typedef const FString * (* _FText_ToString)(const FText * text);
static RelocAddr <_FText_ToString> FText_ToString(0x00E63E60);

const FString * FText::ToString() const
{
	return FText_ToString(this);
}

void FText::Release()
{
	u8 * controller = m_referenceController;
	if(!controller) return;

	auto ** vtable = *reinterpret_cast <void ***>(controller);
	if(_InterlockedDecrement(reinterpret_cast <volatile long *>(controller + 8)) == 0)
	{
		reinterpret_cast <void (*)(void *)>(vtable[0])(controller);	// destroys the data
		if(_InterlockedDecrement(reinterpret_cast <volatile long *>(controller + 0xC)) == 0)
			reinterpret_cast <void (*)(void *, u32)>(vtable[1])(controller, 1);	// and itself
	}
}
