#pragma once

#include "GameScript.h"
#include <vector>

// Classic OBSE's user functions: a script whose block is "Begin Function { parameters }", run by
// an event handler with its parameters set from the event's arguments.

// Runs the function script with these arguments (numbers, or forms as their form IDs); false if
// it isn't a function script or couldn't be run.
bool CallUserFunction(Script * script, TESObjectREFR * thisObj, const std::vector <double> & args);

// makes the game run a Function block when, and only when, its script is called as a function
void InstallUserFunctionBlock();
