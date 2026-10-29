This is a community maintained up-to-date fork of **Oblivion Remastered Script Extender** (OBSE64).

xOBSE64 brings classic OBSE's script commands to Oblivion Remastered, each at the opcode classic OBSE gave it, so that mods made for classic Oblivion with OBSE can run their scripts on the Remastered as they are. It is based on OBSE64 0.2.2 and supports the Steam version of Oblivion Remastered, 1.512.105.

## Download

[Download the latest xOBSE64](https://github.com/rheard/xobse64/releases/latest)

All releases are in the [releases section](https://github.com/rheard/xobse64/releases), and what each one changed is in [CHANGELOG.md](CHANGELOG.md).

## Installation

1. If you have OBSE64 installed, keep a copy of its `obse64_1_512_105.dll` and `obse64_loader.exe`, to go back to it later.
2. Copy `obse64_1_512_105.dll` and `obse64_loader.exe` from the zip into the game's `OblivionRemastered\Binaries\Win64` folder, usually `C:\Program Files (x86)\Steam\steamapps\common\Oblivion Remastered\OblivionRemastered\Binaries\Win64`. Both files: OBSE64's own loader only starts its own DLL.
3. Make sure Steam is running, then start the game with `obse64_loader.exe`.

OBSE64 plugins still go in `Binaries\Win64\OBSE\Plugins`, and mods' plugins in `Content\Dev\ObvData\Data`, as before. Once xOBSE64 is running, the version text the game shows in its menus ends with `[xOBSE64 0.3.0]` (or whichever version you have), and the first line of `Documents\My Games\Oblivion Remastered\OBSE\Logs\obse64.txt` starts with `xOBSE64 runtime`.

## Building

You need Git, and Visual Studio 2022 or later with the "Desktop development with C++" workload, which includes CMake.

```
git clone https://github.com/rheard/xobse64
cmake -B xobse64/build -S xobse64
cmake --build xobse64/build --config Release
```

That builds `xobse64/build/obse64/Release/obse64_1_512_105.dll` and `xobse64/build/obse64_loader/Release/obse64_loader.exe`. The Visual Studio solution is `xobse64/build/umbrella.sln` (`umbrella.slnx` with Visual Studio 2026).

## Support

For bug reports, a classic mod that doesn't work, and other problems, [open a GitHub issue](https://github.com/rheard/xobse64/issues). For a mod that doesn't work, name the mod and attach `obse64.txt` from a game where you used it: it names each command a script called that isn't ported yet.

**xOBSE64 is not made or supported by the OBSE64 team. Please don't report problems with it to OBSE64's authors.**

## Description

**Oblivion Remastered Script Extender** (OBSE64) is a modder's resource that expands the scripting capabilities of The Elder Scrolls IV: Oblivion Remastered. It does so without modifying the executable files on disk, so there are no permanent side effects.

A compiled script stores each command it calls as a number, its opcode, and classic OBSE numbered its commands in the order it added them. OBSE64 0.2.2 has classic's first 78, through classic's release 6. xOBSE64 adds every classic command after those, 0x144E to 0x1A1B, each at its classic opcode, so a script compiled against classic OBSE finds each command where it was compiled to look:

- A **ported** command does what it did in classic, on the Remastered's engine.
- Every other command is a **placeholder** for now. The first time a script calls one, it writes `classic command <name> (<opcode>) called by script <form ID> is not implemented yet` to `obse64.txt`, and a shorter line to the console, and that line of the script fails, as it would have without xOBSE64.

The Remastered runs Oblivion's own engine underneath, with Unreal drawing it, so most commands can keep classic's behavior. Where the Remastered works differently (input, menus, and what Unreal handles), a command keeps classic's behavior as far as the Remastered allows, and its commit message says where it can't.

For what each command does, see xOBSE's [command documentation](https://github.com/llde/xOBSE/blob/master/obse_command_doc.html) and the [CS Wiki](https://cs.elderscrolls.com/index.php?title=Main_Page).

### Ported so far

96 commands, and classic's copies of 15 console commands for scripts, on top of OBSE64's own:

- **Printing**: `DebugPrint`, `PrintD`, `SetDebugMode`, `TestExpr`, `MessageEX` and `MessageBoxEX`, with classic's format specifiers (`%n` for a form's name, `%i` for its form ID, `%k` for a key's name, and the rest), which OBSE64's `PrintToConsole` now takes too. What scripts print appears in the console and in `obse64.txt`.
- **Script flow**: `Label` and `GoTo` (`SaveIP`, `RestoreIP`); `ForEach` over a container's items, with `Loop`, `Break` and `Continue`; user functions (`Begin Function`, `Call`, `SetFunctionValue`); and event handlers (`SetEventHandler`, `RemoveEventHandler`) for `OnKeyEvent` and `OnControlEvent`.
- **Strings**: `sv_Construct`, `sv_Set`, `sv_SubString`, `sv_Length`, `sv_Destruct`.
- **Forms and references**: `GetBaseObject`, `GetObjectType`, the type checks from `IsWeapon` to `IsFlora`, `IsCreature`, `GetFirstRef`, `GetNextRef`, `GetCrosshairRef`, `GetRider`, `IsQuestItem`, `SetQuestItem`, `IsRefEssential`, `SetRefEssential`, `HasSpell`, and the model path commands (`CompareModelPath`, `CopyModelPath`, `CompareMaleGroundPath`, `CopyMaleGroundPath`).
- **Ownership and crime**: `GetOwner`, `GetParentCellOwner`, `GetOwningFactionRequiredRank`, `GetParentCellOwningFactionRequiredRank`, `IsOffLimits`, `GetGoldValue`.
- **Inventory**: `AddItemNS`, `RemoveItemNS`, `EquipItemNS`, `UnequipItemNS`; an item's health (`GetObjectHealth`, `GetCurrentHealth`, `SetCurrentHealth`, and the `EquippedCurrentHealth` commands) and charge (`GetObjectCharge`, `GetCurrentCharge`, `SetCurrentCharge`, `ModCurrentCharge`, and the `EquippedCurrentCharge` commands); the poison on the weapon in hand (`GetEquippedWeaponPoison`, `SetEquippedWeaponPoison`, `RemoveEquippedWeaponPoison`); and the quick keys (`GetHotKeyItem`, `SetHotKeyItem`, `ClearHotKey`).
- **Input**: `GetNumKeysPressed`, `GetKeyPress`, `IsControlPressed`, `OnControlDown`, `DisableControl`, `EnableControl` and `TapControl`. The control commands work on moving, sprinting and the walk toggle so far, and say so in `obse64.txt` the first time they're asked about another control.
- **Other**: `GetGameLoaded`, `GetGameRestarted`, `FileExists`, `RunBatchScript`, `GetActiveMenuSelection` (in the magic menu), and the `con_` console commands (`con_TCL`, `con_TGM`, `con_Save` and the rest).

The game's own `IsRunning` also answers as classic's did for the player on foot. The Remastered's said no while standing still, even with Always Run on.

### Not yet

- Most of classic's commands: the rest of the slots are placeholders.
- Arrays (`ar_` commands), most string commands, and `While`, `let` and `eval`, which need classic's expression operators.
- Strings aren't saved with the game, so a string a mod keeps is gone after loading a save.
- `ForEach` and `GoTo` don't work inside function scripts yet.
- Events other than `OnKeyEvent` and `OnControlEvent`.
- OBSE64's mouse commands (`MoveMouseX`, `MoveMouseY`, `SetMouseSpeedX`, `SetMouseSpeedY`, `DisableMouse`, `EnableMouse`) still do nothing.
- `GetOBSEVersion` reports classic version 10, so a mod that requires a later version won't run.

## Creating OBSE64 plugins (for developers)

xOBSE64 keeps OBSE64's plugin API (`obse64/PluginAPI.h`) unchanged, and reports OBSE64's version, 0.2.2, to plugins, so plugins built for OBSE64 work with it. OBSE64 plugins can't add script commands, which is why classic's commands come as a fork of OBSE64 rather than as a plugin.

## Contributing

Pull requests are welcome, above all ports of the commands a mod you want to run needs.

- A command goes in its classic slot, under its classic name and alias (`obse64/ClassicCommandTable.inl`), and into `kPortedCommands` in `obse64/Commands_Classic.cpp` with its return type.
- Its parameters must be exactly classic's: the game reads a compiled script's arguments with them.
- Classic OBSE's source, in [xOBSE](https://github.com/llde/xOBSE), is the reference for what a command does. Write the port against OBSE64's types and the Remastered's engine, rather than copying classic's code.
- Test it in game, with a mod that uses it if there is one, and say in the commit message where it differs from classic and why.

## FAQ / Troubleshooting

Does xOBSE64 replace OBSE64?
- Yes: it's OBSE64 with classic's commands added. Install one or the other. OBSE64 plugins work with it.

The game won't start, and says "Bad OBSE64 DLL":
- You're starting it with OBSE64's own loader, which only starts OBSE64's own DLL. Copy xOBSE64's `obse64_loader.exe` over it too.

Can I switch to xOBSE64, or back to OBSE64, in the middle of a playthrough?
- Yes. xOBSE64 keeps nothing of its own in your saves yet. Mods that need its commands stop working without it.

A classic mod doesn't work:
- Look in `obse64.txt` for lines ending `is not implemented yet`: each names a command a script called that isn't ported. Open an issue naming the mod, with those lines.
- Some classic mods use assets, such as models and animations made for classic Oblivion, which cannot carry over Oblivion Remastered easily. If you're trying to load a classic mod, make sure it is a script-only mod.

Do classic OBSE plugins (`.dll` files, such as MenuQue or ConScribe) work?
- No. They're 32-bit programs made for classic Oblivion's executable, and the commands they added aren't in xOBSE64, so mods that need them won't work either.

Can I use the commands in the console?
- Yes, most of them, as in a script: `player.GetEquippedCurrentHealth 16`, for example.

Where are the logs?
- `Documents\My Games\Oblivion Remastered\OBSE\Logs`. `obse64.txt` also gets a copy of each line a script prints, marked `console:`. To turn that copy off, put this in `Binaries\Win64\OBSE\obse.ini`:

  ```
  [Debug]
  LogScriptPrints=0
  ```

Is the Game Pass, Microsoft Store or Epic Games version supported?
- No. Like OBSE64, xOBSE64 supports the Steam version only.

My virus scanner complains about `obse64_loader.exe`:
- As with OBSE64, that's a false positive: the loader injects xOBSE64's DLL into the game, a technique some viruses use too. Add an exception for it.

## Credits

OBSE64 is made by *Ian Patterson* (ianpatt).

Classic OBSE was created and maintained by *Ian Patterson*, *Stephen Abel*, *Paul Connelly* and *Madeesh Kannan* (ianpatt, behippo, scruggsywuggsy the ferret, and shadeMe), with additional contributions from *Timeslip*, *The J*, *DragoonWraith*, *SkyRanger-1*, *badhair*, *JRoush* and *kyoma*.

[xOBSE](https://github.com/llde/xOBSE), the community maintained fork of classic OBSE, is maintained by *llde* and *shadeMe*.

xOBSE64 is maintained by *rheard*.

## Thanks

Thanks to *ianpatt* for OBSE64, which everything here is built on, and to the xOBSE maintainers and contributors, whose source is the reference for every command ported here.

Thanks to the authors of the classic mods xOBSE64 is tested with.
