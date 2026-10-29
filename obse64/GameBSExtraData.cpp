#include "GameBSExtraData.h"
#include "obse64_common/Relocation.h"
#include "obse64_common/Log.h"
#include <Windows.h>

RelocPtr <CRITICAL_SECTION> g_extraListCS(0x0947FF00);

RelocAddr <_GameHeap_Allocate> GameHeap_Allocate(0x04702CB0);
RelocAddr <_ExtraDataList_Construct> ExtraDataList_Construct(0x06614FC0);
RelocAddr <_BaseExtraList_Add> BaseExtraList_Add(0x06615310);
RelocAddr <_BaseExtraList_Remove> BaseExtraList_Remove(0x0661D210);
RelocAddr <_BaseExtraList_CopyFrom> BaseExtraList_CopyFrom(0x066166E0);
RelocAddr <_BaseExtraList_RemoveAll> BaseExtraList_RemoveAll(0x0661CCC0);
RelocAddr <_BaseExtraList_SetScriptLocals> BaseExtraList_SetScriptLocals(0x06621D40);

void BaseExtraList::Lock()
{
	EnterCriticalSection(g_extraListCS);
}

void BaseExtraList::Unlock()
{
	LeaveCriticalSection(g_extraListCS);
}

BSExtraData * BaseExtraList::Get(u8 type) const
{
	BSExtraData * result = nullptr;

	if(Contains(type))
	{
		for(auto * iter = m_data; iter; iter = iter->m_next)
		{
			if(iter->m_type == type)
			{
				result = iter;
				break;
			}
		}

		if(!result)
		{
			_WARNING("BaseExtraList: mismatch between presence bitfield and data %02X", type);
		}
	}

	return result;
}

bool BaseExtraList::Contains(u8 type) const
{
	if(type >= kExtraData_Max)
		return false;

	return (m_presence[type >> 3] >> (type & 7)) & 1;
}
