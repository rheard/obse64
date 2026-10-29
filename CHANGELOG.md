# Changelog

xOBSE64's changes, release by release, newest first. Write each change up here as it's made, under the coming release's heading, so the release's notes are ready when it ships: a release's section becomes its notes on GitHub.

## 0.3.0

The first release of xOBSE64, based on OBSE64 0.2.2, for the Steam version of Oblivion Remastered, 1.512.105.

### What's new

Classic OBSE's script commands are back at the opcodes classic OBSE gave them. Every classic command after OBSE64's own, 0x144E to 0x1A1B, is registered in its classic slot, so a script compiled against classic OBSE finds each command where it was compiled to look. A command that isn't ported yet is a placeholder: the first time a script calls it, it names itself in `obse64.txt` and the console, and that line of the script fails.

Ported in this release, 96 commands and classic's copies of 15 console commands:

- **Printing**: `DebugPrint`, `PrintD`, `SetDebugMode`, `TestExpr`, `MessageEX` and `MessageBoxEX`, with classic's format specifiers (`%n` for a form's name, `%i` for its form ID, `%k` for a key's name, and the rest).
- **Script flow**: `Label` and `GoTo` (`SaveIP`, `RestoreIP`); `ForEach` over a container's items, with `Loop`, `Break` and `Continue`; user functions (`Begin Function`, `Call`, `SetFunctionValue`); and event handlers (`SetEventHandler`, `RemoveEventHandler`) for `OnKeyEvent` and `OnControlEvent`.
- **Strings**: `sv_Construct`, `sv_Set`, `sv_SubString`, `sv_Length`, `sv_Destruct`.
- **Forms and references**: `GetBaseObject`, `GetObjectType`, the type checks from `IsWeapon` to `IsFlora`, `IsCreature`, `GetFirstRef`, `GetNextRef`, `GetCrosshairRef`, `GetRider`, `IsQuestItem`, `SetQuestItem`, `IsRefEssential`, `SetRefEssential`, `HasSpell`, and the model path commands (`CompareModelPath`, `CopyModelPath`, `CompareMaleGroundPath`, `CopyMaleGroundPath`).
- **Ownership and crime**: `GetOwner`, `GetParentCellOwner`, `GetOwningFactionRequiredRank`, `GetParentCellOwningFactionRequiredRank`, `IsOffLimits`, `GetGoldValue`.
- **Inventory**: `AddItemNS`, `RemoveItemNS`, `EquipItemNS`, `UnequipItemNS`; an item's health (`GetObjectHealth`, `GetCurrentHealth`, `SetCurrentHealth`, `GetEquippedCurrentHealth`, `SetEquippedCurrentHealth`, `ModEquippedCurrentHealth`) and charge (`GetObjectCharge`, `GetCurrentCharge`, `SetCurrentCharge`, `ModCurrentCharge`, `GetEquippedCurrentCharge`, `SetEquippedCurrentCharge`, `ModEquippedCurrentCharge`); the poison on the weapon in hand (`GetEquippedWeaponPoison`, `SetEquippedWeaponPoison`, `RemoveEquippedWeaponPoison`); and the quick keys (`GetHotKeyItem`, `SetHotKeyItem`, `ClearHotKey`).
- **Input**: `GetNumKeysPressed`, `GetKeyPress`, `IsControlPressed`, `OnControlDown`, `DisableControl`, `EnableControl` and `TapControl`. The control commands work on moving, sprinting and the walk toggle so far.
- **Other**: `GetGameLoaded`, `GetGameRestarted`, `FileExists`, `RunBatchScript`, `GetActiveMenuSelection` (in the magic menu), and the `con_` console commands (`con_TCL`, `con_TGM`, `con_Save` and the rest).

### Minor features

- `GetOBSEVersion` returns classic OBSE's version 10, which some classic mods require.
- What scripts print shows in the console and is copied to `obse64.txt`, marked `console:`. `[Debug] LogScriptPrints=0` in `obse.ini` turns the copy off.
- `PrintToConsole` takes classic OBSE's format specifiers, as `DebugPrint` does, and prints long text in pieces, as classic did.
- The game's own `IsRunning` answers as classic's did for the player on foot. The Remastered's said no while standing still, even with Always Run on.
- xOBSE64 has a version of its own, shown in its files' properties, its logs, the version text in the game's menus, and what `GetOBSEVersion` prints in the console. Plugins are still given OBSE64's, 0.2.2, the plugin interface xOBSE64 has.
- A plugin that needs a newer OBSE64 than 0.2.2 says so in the plugin error box, instead of asking for a newer script extender.

### Fixes to OBSE64 0.2.2

- Commands that return a form, such as `GetEquippedObject` and `GetActiveSpell`, return one scripts can use. 0.2.2 stored forms the way classic Oblivion did, which the Remastered reads back as no form.
- Loading a save no longer crashes in `GetActiveSpell`: `PlayerCharacter::Get` reads the player from its pointer.
- `PrintToConsole` no longer ends the game on a mod's `%n`. 0.2.2 passed the text to C's `snprintf`, which rejects it.
- What scripts print shows in the console. The game only prints around a command typed in the console, so 0.2.2 dropped every line a running script printed.
- The key commands work: `IsKeyPressed2`, `TapKey`, `HoldKey`, `ReleaseKey`, `HammerKey`, `AHammerKey`, `UnHammerKey`, `DisableKey` and `EnableKey` were placeholders.
- `ATan` returns the arctangent, in degrees. 0.2.2's took the tangent, so `ATan 1` gave 89.2 instead of 45.
- `Rand` never returns its maximum, as classic's didn't. 0.2.2 rounded the number to a float, which could land on it: `Rand 100000 100001` gave 100001 about once in 256 calls.
- `Log10` and `Tan` (in degrees) go by their own names in the console and `RunBatchScript`. 0.2.2 named them `Exp` and `Tanh`, so typing `Tanh` ran `Tan`.
- A DLL in `OBSE\Plugins` that exports nothing no longer stops the plugins after it from loading.
- Once a plugin fails to load, the other plugins' messages still reach the right plugins, and the failed plugin's message handlers are dropped before it's unloaded, where they would have been called into unloaded code.
- For plugin authors: `RUNTIME_VERSION_1_512_105` in `obse64_version.h` is 1.512.105. In 0.2.2 it was 1.512.102, so a plugin that listed it among the game versions it supports was refused.
- For plugin authors: `ExtraAltarBPVariant::kID` in `AltarExtraData.h` is 0x5D, the type the game gives that extra data. In 0.2.2 it was `ExtraAltarActorGUID`'s, 0x5E, so `BaseExtraList::Get<ExtraAltarBPVariant>()` returned a reference's actor GUID instead.
