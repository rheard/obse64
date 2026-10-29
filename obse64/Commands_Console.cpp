#include "Commands_Classic.h"
#include "GameConsole.h"
#include "GameScript.h"

/* Print formatted string to Oblivion console
 * syntax: PrintToConsole fmtstring num1 num2 ...
 * shortname: printc
 *
 * Prints a formatted string to the Oblivion console, as classic OBSE does.
 * -Numbers use printf notation (%#.#f %g %e), and OBSE's own specifiers work too, such as %n for a
 *  form's name and %i for its ID, so classic mods' format strings print as they did.
 * -You can pass up to 20 variables, but you don't have to pass any at all.
 * -Returns 1 if it printed.
 */
bool Cmd_PrintToConsole_Execute(COMMAND_ARGS)
{
	*result = 0;

	// not snprintf: classic mods pass OBSE's specifiers, and snprintf rejects %n, which ends the game
	std::string text;
	if(ExtractClassicFormatString(PASS_COMMAND_ARGS, &text))
	{
		ScriptConsolePrint(text.c_str());
		*result = 1;
	}

	return true;
}

CommandInfo kCommandInfo_PrintToConsole =
{
	"PrintToConsole", "printc",
	0,
	"Print formatted string to console",
	0,
	21, kParams_FormatString,
	Cmd_PrintToConsole_Execute
};
