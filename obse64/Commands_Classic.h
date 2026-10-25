#pragma once

#include "GameScript.h"
#include <string>

// classic OBSE's format string parameters: the string and up to 20 numbers
extern ParamInfo kParams_FormatString[21];

// Reads a command's format string and up to 20 numbers and expands it as classic OBSE does, with
// its own specifiers (%n for a form's name, %i for its ID, ...). False if the arguments don't fit.
bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out);
