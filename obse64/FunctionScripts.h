#pragma once

#include "GameScript.h"
#include "ObseExpression.h"
#include <vector>

// Classic OBSE's user functions: a script whose block is "Begin Function { parameters }", run by
// Call or by an event handler, with its parameters set from the arguments, and returning what it
// gives SetFunctionValue.

// Runs the function script with these arguments; false if it isn't a function script or couldn't
// be set up to run. result, if given, receives what the function returned (nothing if it didn't).
bool CallUserFunction(Script * script, TESObjectREFR * thisObj, TESObjectREFR * containingObj,
	const std::vector <ObseExpression::Value> & args, ObseExpression::Value * result = nullptr);

// makes the game run a Function block when, and only when, its script is called as a function
void InstallUserFunctionBlock();
