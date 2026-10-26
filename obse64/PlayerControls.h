#pragma once

#include "obse64_common/Types.h"

// Classic's controls in the Remastered. Unreal's input handlers pass some on to the game's
// controls (classic's OSInputGlobals), where IsControlPressed reads them; the rest, movement (0 to
// 3), sprinting (classic's Run, 10) and the walk/run toggle (classic's Always Run, 11), Unreal
// handles itself, on its game thread. These are called on the Oblivion thread.

// whether one of the controls Unreal handles itself is down; false if the control isn't one
bool PlayerControls_GetDown(u32 control, bool * down);

// stops the player using a control, or lets them again; false if that isn't possible for it
bool PlayerControls_SetDisabled(u32 control, bool disabled);

// presses a control for a moment, as the player would; false if that isn't possible for it
bool PlayerControls_Tap(u32 control);

// does what was asked of Unreal, once each Unreal frame, on its game thread
void PlayerControls_Update();

void PlayerControls_Install();
