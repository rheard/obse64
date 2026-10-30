#pragma once

#include "GameExtraData.h"
#include <vector>

class TESForm;
class TESObjectREFR;

// Classic OBSE's inventory references: a temporary reference standing for one stack of items in a
// container (ForEach gives a script one for each stack in turn), so that commands can be called on
// it. It's made with the game's own constructor, which gives it a form ID scripts can look it up
// by, and is given the stack's item and a copy of its extra data, as the reference the game runs an
// item's script on is (0x06647F90). Unlike classic, the copy isn't written back to the stack: a
// command changes the stack itself where it knows to (the charge and health commands do).

// one stack of items in a container: the items of an entry sharing an extra data list, or those
// with none of their own
struct InventoryStack
{
	TESObjectREFR					* container = nullptr;
	TESForm							* item = nullptr;
	ExtraContainerChanges::Entry	* entry = nullptr;	// none for an item only the base container has
	ExtraDataList					* list = nullptr;	// none for the items without extra data of their own
	s32								count = 0;
};

// the container's stacks, in the order classic's ForEach gives them: its changes' entries, then
// what's left of its base container
void GetInventoryStacks(TESObjectREFR * container, std::vector <InventoryStack> * out);

// whether the stack's entry, and list if it has one, are still in its container
bool IsInventoryStackValid(const InventoryStack & stack);

// a new inventory reference, standing for nothing yet; null if the game didn't give it a form ID
TESObjectREFR * CreateInventoryRef();

// points the reference at a stack
void SetInventoryRef(TESObjectREFR * ref, const InventoryStack & stack);

void DestroyInventoryRef(TESObjectREFR * ref);

bool IsInventoryRef(TESObjectREFR * ref);

// The extra data list of the stack an inventory reference stands for, while the stack is still in
// its container. With create, a stack of items without one of their own is given one, which all
// of them then share; an item only the base container holds is first given an entry in the
// container's changes, which a container has once its inventory has changed at all.
ExtraDataList * GetInventoryRefList(TESObjectREFR * ref, bool create);

// gives an inventory reference a new copy of its stack's extra data, after a command changed it
void RefreshInventoryRef(TESObjectREFR * ref);
