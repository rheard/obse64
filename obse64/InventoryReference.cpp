#include "InventoryReference.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include <algorithm>
#include <cstring>
#include <map>

// TESObjectREFR's constructor, which gives the reference a new form ID and adds it to the game's
// forms. The game makes the reference it runs items' scripts on with it too (0x0663D620), then
// makes that one temporary (0x06612830), which takes the form ID away again.
typedef TESObjectREFR * (* _TESObjectREFR_Construct)(void * mem);
static RelocAddr <_TESObjectREFR_Construct> TESObjectREFR_Construct(0x065782D0);

// a simple list's add, which puts the new item first, as UnequipItem gives an entry's items a list
// of their own (0x06A20B70)
typedef void (* _BSSimpleList_AddFirst)(void * list, void * item);
static RelocAddr <_BSSimpleList_AddFirst> BSSimpleList_AddFirst(0x06579740);

// TESForm's virtual deleting destructor, through which the game deletes a form
enum { kVirtual_DeletingDestructor = 4 };

static std::map <TESObjectREFR *, InventoryStack> s_inventoryRefs;

static s32 GetListCount(const ExtraDataList * list)
{
	auto * xCount = list->Get<ExtraCount>();
	return xCount ? xCount->count : 1;
}

// how many of an item a container's base container holds (a leveled list is never the item)
static s32 GetBaseCount(TESObjectREFR * container, TESForm * item)
{
	TESForm * baseForm = container->GetBaseForm();
	auto * baseContainer = baseForm ? DYNAMIC_CAST(baseForm, TESForm, TESContainer) : nullptr;

	s32 count = 0;
	for(auto * node = baseContainer ? &baseContainer->list.node : nullptr; node; node = node->m_next)
	{
		if(node->m_data && node->m_data->type == item)
			count += node->m_data->count;
	}

	return count;
}

// how many of an entry's items have no extra data list of their own
static s32 GetUnlistedCount(TESObjectREFR * container, ExtraContainerChanges::Entry * entry)
{
	s32 count = GetBaseCount(container, entry->type) + entry->countDelta;
	for(auto * node = entry->extraData ? &entry->extraData->node : nullptr; node; node = node->m_next)
	{
		if(node->m_data)
			count -= GetListCount(node->m_data);
	}

	return count;
}

void GetInventoryStacks(TESObjectREFR * container, std::vector <InventoryStack> * out)
{
	out->clear();
	if(!container) return;

	BaseExtraList::Locker lock;

	// what the base container holds, but for leveled lists, which are resolved into the changes
	std::map <TESForm *, s32> baseCounts;
	std::vector <TESForm *> baseItems;

	TESForm * baseForm = container->GetBaseForm();
	if(auto * baseContainer = baseForm ? DYNAMIC_CAST(baseForm, TESForm, TESContainer) : nullptr)
	{
		for(auto * node = &baseContainer->list.node; node; node = node->m_next)
		{
			auto * entry = node->m_data;
			if(!entry || !entry->type || DYNAMIC_CAST(entry->type, TESForm, TESLevItem)) continue;

			if(!baseCounts.count(entry->type))
				baseItems.push_back(entry->type);
			baseCounts[entry->type] += entry->count;
		}
	}

	auto * changes = container->extraList.Get<ExtraContainerChanges>();
	if(auto * objList = changes ? changes->GetObjList() : nullptr)
	{
		for(auto * node = &objList->node; node; node = node->m_next)
		{
			auto * entry = node->m_data;
			if(!entry || !entry->type) continue;

			// an entry counts the change to what the base container has
			s32 total = entry->countDelta;
			auto base = baseCounts.find(entry->type);
			if(base != baseCounts.end())
			{
				total += base->second;
				base->second = 0;
			}

			// the items sharing each extra data list, then the rest, which have none of their own
			s32 listed = 0;
			for(auto * listNode = entry->extraData ? &entry->extraData->node : nullptr; listNode && listed < total; listNode = listNode->m_next)
			{
				if(!listNode->m_data) continue;

				s32 count = std::min(GetListCount(listNode->m_data), total - listed);
				out->push_back({ container, entry->type, entry, listNode->m_data, count });
				listed += count;
			}

			if(total > listed)
				out->push_back({ container, entry->type, entry, nullptr, total - listed });
		}
	}

	for(TESForm * item : baseItems)
	{
		s32 count = baseCounts[item];
		if(count > 0)
			out->push_back({ container, item, nullptr, nullptr, count });
	}
}

bool IsInventoryStackValid(const InventoryStack & stack)
{
	// the base container's items don't change
	if(!stack.entry) return true;

	BaseExtraList::Locker lock;

	auto * changes = stack.container ? stack.container->extraList.Get<ExtraContainerChanges>() : nullptr;
	auto * objList = changes ? changes->GetObjList() : nullptr;
	for(auto * node = objList ? &objList->node : nullptr; node; node = node->m_next)
	{
		if(node->m_data != stack.entry) continue;

		if(stack.entry->type != stack.item) return false;
		if(!stack.list) return true;

		for(auto * listNode = stack.entry->extraData ? &stack.entry->extraData->node : nullptr; listNode; listNode = listNode->m_next)
		{
			if(listNode->m_data == stack.list)
				return true;
		}

		return false;
	}

	return false;
}

static void DeleteRef(TESObjectREFR * ref)
{
	typedef void (* _DeletingDestructor)(TESObjectREFR * ref, u32 flags);
	(*reinterpret_cast <_DeletingDestructor * const *>(ref))[kVirtual_DeletingDestructor](ref, 1);
}

// The game's RemoveAll and CopyFrom free a list's extra data without clearing its bits in the
// list's presence bitfield (its remove of one, 0x0661D210, clears them), so a reference given
// another stack went on saying it had what the stack before it had: the charge of a stack before,
// with none behind the bit. Sets the bits from the extra data the list has.
static void SyncPresenceBits(BaseExtraList * list)
{
	memset(list->m_presence, 0, sizeof(list->m_presence));

	for(BSExtraData * data = list->m_data; data; data = data->m_next)
	{
		if(data->m_type < kExtraData_Max)
			list->m_presence[data->m_type >> 3] |= u8(1 << (data->m_type & 7));
	}
}

// takes a stack's extra data away from the reference, as the game does after an item's script
static void ClearExtraData(TESObjectREFR * ref)
{
	BaseExtraList_SetScriptLocals(&ref->extraList, nullptr);
	BaseExtraList_RemoveAll(&ref->extraList, true);
	SyncPresenceBits(&ref->extraList);
}

TESObjectREFR * CreateInventoryRef()
{
	void * mem = GameHeap_Allocate(sizeof(TESObjectREFR));
	if(!mem) return nullptr;

	// the constructor gives no form ID while the game is loading
	TESObjectREFR * ref = TESObjectREFR_Construct(mem);
	if(!ref->refID)
	{
		DeleteRef(ref);
		return nullptr;
	}

	s_inventoryRefs[ref] = InventoryStack();

	return ref;
}

void SetInventoryRef(TESObjectREFR * ref, const InventoryStack & stack)
{
	BaseExtraList::Locker lock;

	ClearExtraData(ref);

	ref->baseForm = stack.item;
	if(stack.list)
	{
		BaseExtraList_CopyFrom(&ref->extraList, stack.list);
		SyncPresenceBits(&ref->extraList);
	}

	s_inventoryRefs[ref] = stack;
}

void DestroyInventoryRef(TESObjectREFR * ref)
{
	{
		BaseExtraList::Locker lock;

		ClearExtraData(ref);
		ref->baseForm = nullptr;
	}

	s_inventoryRefs.erase(ref);

	DeleteRef(ref);
}

bool IsInventoryRef(TESObjectREFR * ref)
{
	return s_inventoryRefs.count(ref) != 0;
}

ExtraDataList * GetInventoryRefList(TESObjectREFR * ref, bool create)
{
	auto found = s_inventoryRefs.find(ref);
	if(found == s_inventoryRefs.end()) return nullptr;

	InventoryStack & stack = found->second;
	if(!stack.item || !IsInventoryStackValid(stack)) return nullptr;

	// only the base container's items have no entry to give a list to
	if(stack.list || !create || !stack.entry) return stack.list;

	BaseExtraList::Locker lock;

	// the items the new list is for, counted now: the script may have added or removed some since
	// its loop came to the stack, and a list mustn't claim more than the entry has
	s32 count = GetUnlistedCount(stack.container, stack.entry);
	if(count <= 0) return nullptr;

	if(!stack.entry->extraData)
	{
		auto * head = static_cast <BSSimpleList <ExtraDataList *> *>(GameHeap_Allocate(sizeof(BSSimpleList <ExtraDataList *>)));
		if(!head) return nullptr;

		head->node.m_data = nullptr;
		head->node.m_next = nullptr;
		stack.entry->extraData = head;
	}

	void * mem = GameHeap_Allocate(sizeof(ExtraDataList));
	if(!mem) return nullptr;

	ExtraDataList * list = ExtraDataList_Construct(mem);

	if(count > 1)
	{
		if(void * countMem = GameHeap_Allocate(sizeof(ExtraCount)))
			BaseExtraList_Add(list, ExtraCount_Construct(countMem, s16(std::min(count, 0x7FFF))));
	}

	BSSimpleList_AddFirst(stack.entry->extraData, list);
	stack.list = list;
	stack.count = count;

	return list;
}

void RefreshInventoryRef(TESObjectREFR * ref)
{
	auto found = s_inventoryRefs.find(ref);
	if(found != s_inventoryRefs.end() && found->second.item && IsInventoryStackValid(found->second))
	{
		InventoryStack stack = found->second;
		SetInventoryRef(ref, stack);
	}
}
