#include "FormatString.h"
#include "Commands_Classic.h"
#include "GameForms.h"
#include "GameTypes.h"
#include "UnrealTypes.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <Windows.h>

// Classic OBSE's format strings, as PrintToConsole, DebugPrint, MessageEX, sv_Construct and the
// rest take them: classic's own specifiers (%n, %i, %z, ...) and printf's for numbers.

static TESForm * FormFromValue(double value)
{
	u32 formID = FormIDFromValue(value);
	return formID ? LookupFormByID(formID) : nullptr;
}

// Many of the Remastered's names are keys that Unreal turns into the text it shows: "LOC_" ones
// from its string tables (LOC_FN_Phintias), "UI_" ones from its table of text the player typed
// (the player's name). 0x04883D70 makes Unreal's text for a key in a std::string, through
// 0x04883EA0, which builds its tables the first time (the byte at 0x09306CD0 says it has), and
// FText::ToString (0x00E63E60) gives what it shows. Asked from the thread scripts run on, a "UI_"
// key comes back right, but a "LOC_" key comes back as "<MISSING STRING TABLE ENTRY>": those are
// left as they are, and MessageEX and MessageBoxEX have Unreal translate them.
typedef FText * (* _TextFromKey)(FText * out, const GameStdString * key);
static RelocAddr <_TextFromKey> TextFromKey(0x04883D70);

static RelocPtr <u8> g_textKeyTablesBuilt(0x09306CD0);

// A form's name, with a "UI_" key turned into the text the game shows (characters past Latin-1
// become '?'); a "LOC_" key stays a key
static std::string DisplayName(TESForm * form)
{
	const char * name = GetFullName(form);
	if(strncmp(name, "UI_", 3) || !*g_textKeyTablesBuilt) return name;

	GameStdString key = ToGameStdString(name);

	FText text = {};
	TextFromKey(&text, &key);

	std::string shown;
	if(text.m_data)
	{
		const FString * str = text.ToString();
		const TArray <wchar_t> * chars = str ? &str->m_data : nullptr;
		if(chars && chars->m_data)
		{
			// the length counts the terminator
			for(u32 i = 0; i + 1 < chars->m_len && chars->m_data[i]; i++)
				shown += (chars->m_data[i] < 0x100) ? char(chars->m_data[i]) : '?';
		}
	}

	text.Release();

	return shown.empty() ? name : shown;
}

// A key's name for %k, from its classic keycode: a DirectInput scan code (0x80 set for the keys
// with an E0 prefix), or from 256 a mouse button. Classic had DirectInput's names; this asks
// Windows for the key's name on the keyboard in use.
static std::string KeyName(u32 keycode)
{
	static const char * kMouseButtons[] = { "Left Mouse Button", "Right Mouse Button", "Middle Mouse Button", "Mouse Button 4", "Mouse Button 5" };
	if(keycode >= 0x100)
		return (keycode - 0x100 < 5) ? kMouseButtons[keycode - 0x100] : "?";

	char name[64];
	LONG lParam = LONG((keycode & 0x7F) << 16) | ((keycode & 0x80) ? (1 << 24) : 0);
	if(!GetKeyNameTextA(lParam, name, sizeof(name))) return "?";

	return name;
}

// Expands a classic OBSE format string. OBSE's own specifiers are replaced in place, and any
// other % takes the next argument as a number, as classic OBSE left those to sprintf. %n and %i
// take a form, which a variable holds as its form ID's number, %z a string variable, and %k a key.
// The ones that need a name table (%c %p %v) still use up their arguments, so later numbers line
// up, but print "?" for now. False if there are too few arguments; numUsed receives how many it took.
static bool FormatClassicString(const char * fmt, const double * args, u32 numArgs, std::string * out, u32 * numUsed)
{
	u32 argIdx = 0;
	auto nextArg = [&](double * value)
	{
		if(argIdx >= numArgs) return false;
		*value = args[argIdx++];
		return true;
	};

	out->clear();

	for(const char * p = fmt; *p; p++)
	{
		if(*p != '%')
		{
			*out += *p;
			continue;
		}

		double value = 0;

		switch(p[1])
		{
			case '%':	*out += '%';	p++;	break;
			case 'r':
			case 'R':	*out += '\n';	p++;	break;
			case 'q':
			case 'Q':	*out += '"';	p++;	break;
			case 'e':
			case 'E':					p++;	break;	// empty: works around the CS rejecting empty strings
			case 'B':	*out += '\2';	p++;	break;	// blue console text on
			case 'b':	*out += '\3';	p++;	break;	// and off
			case '}':					p++;	break;

			case 'a':
			case 'A':
				if(!nextArg(&value)) return false;
				*out += char(int(value));
				p++;
				break;

			case 'n':
			case 'N':
				// the form's name, as the game shows it
				if(!nextArg(&value)) return false;
				*out += DisplayName(FormFromValue(value));
				p++;
				break;

			case 'i':
			case 'I':
			{
				// the form's ID, zeros for none
				if(!nextArg(&value)) return false;

				TESForm * form = FormFromValue(value);

				char buf[16];
				snprintf(buf, sizeof(buf), "%08X", form ? form->refID : 0);
				*out += buf;
				p++;
			}
			break;

			case 'z':
			case 'Z':
				// the string variable's text, nothing for a missing one
				if(!nextArg(&value)) return false;
				if(const std::string * str = LookupStringVar(u32(s64(value))))
					*out += *str;
				p++;
				break;

			case 'k': case 'K':
				if(!nextArg(&value)) return false;
				*out += KeyName(u32(value));
				p++;
				break;

			case 'v': case 'V':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case 'p':
			case 'P':
				if(!nextArg(&value)) return false;
				*out += '?';
				p++;
				if(p[1]) p++;	// the o, s or p that picks the pronoun
				break;

			case 'c':
			case 'C':
				if(!nextArg(&value) || !nextArg(&value)) return false;
				*out += '?';
				p++;
				break;

			case '{':
				// %{text%} keeps text only if the argument is nonzero. Text left out still uses up
				// the numbers its specifiers would have taken, so later ones line up, as classic's
				// did: two for %c, none for %% %q %r, one for any other.
				if(!nextArg(&value)) return false;
				p++;
				if(value == 0)
				{
					const char * close = strstr(p + 1, "%}");
					const char * end = close ? close : p + strlen(p);

					for(const char * spec = strchr(p + 1, '%'); spec && spec < end && spec[1]; spec = strchr(spec + 2, '%'))
					{
						u32 skip = strchr("cC", spec[1]) ? 2 : (strchr("%qQrR", spec[1]) ? 0 : 1);
						argIdx = (argIdx + skip < numArgs) ? argIdx + skip : numArgs;
					}

					p = close ? close + 1 : end - 1;
				}
				break;

			case 'x':
			case 'X':
			{
				if(!nextArg(&value)) return false;
				p++;

				int width = 0;
				if(isdigit((u8)p[1]))	// a single-digit width may follow
					width = *++p - '0';

				char buf[32];
				snprintf(buf, sizeof(buf), "%0*llX", width, (unsigned long long)value);
				*out += buf;
			}
			break;

			default:
			{
				// a printf-style number: flags, width, precision, length, conversion. A length (%lf,
				// %ld) is left out of what's passed on, as each number is a double here; classic's
				// sprintf printed %lf as %f.
				const char * end = p + 1;
				while(*end && strchr("-+ #0", *end)) end++;
				while(isdigit((u8)*end)) end++;
				if(*end == '.')
				{
					end++;
					while(isdigit((u8)*end)) end++;
				}

				const char * length = end;
				while(*end && strchr("hlLjzt", *end)) end++;

				if(!nextArg(&value)) return false;

				char buf[128];
				if(*end && strchr("fFgGeE", *end))
				{
					std::string spec(p, length);
					spec += *end;
					snprintf(buf, sizeof(buf), spec.c_str(), value);
					*out += buf;
				}
				else if(*end && strchr("diu", *end))
				{
					std::string spec(p, length);
					spec += (*end == 'u') ? "llu" : "lld";
					snprintf(buf, sizeof(buf), spec.c_str(), (long long)value);
					*out += buf;
				}
				else
				{
					// not a number conversion: print it as written
					out->append(p, *end ? end + 1 : end);
				}

				p = *end ? end : end - 1;
			}
			break;
		}
	}

	*numUsed = argIdx;

	return true;
}

ParamInfo kParams_FormatString[21] =
{
	{	"format string",	kParamType_String,	0 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
	{	"variable",			kParamType_Float,	1 },
};

// Reads a command's format string and its numbers, as classic OBSE's PrintToConsole does: up to
// 20 for the format string, and one more that some commands take after them. The numbers are read
// one at a time by the game's numeric argument reader, which keeps them as doubles, so a ref
// variable's form ID comes through whole: ExtractArgs would round them to floats.
bool ExtractClassicFormatString(COMMAND_ARGS, std::string * out, std::vector <double> * rest)
{
	u32 offset = *opcodeOffsetPtr;
	u32 end = offset + *(const u16 *)(scriptData + offset - 2);	// the arguments' length is just before them

	// the argument count comes first, and includes the format string
	u16 numArgs = *(const u16 *)(scriptData + offset);
	u16 fmtLen = *(const u16 *)(scriptData + offset + 2);
	offset += 4;

	if(!numArgs || numArgs > 22 || offset + fmtLen > end) return false;

	std::string fmt(scriptData + offset, fmtLen);
	offset += fmtLen;

	double args[21] = { 0 };
	u32 numNumbers = numArgs - 1;
	for(u32 i = 0; i < numNumbers; i++)
	{
		if(offset >= end || !ExtractNumericArg(&args[i], scriptData, &offset, thisObj, containingObj, script, locals, true))
			return false;
	}

	*opcodeOffsetPtr = offset;

	u32 numUsed = 0;
	if(!FormatClassicString(fmt.c_str(), args, numNumbers, out, &numUsed)) return false;

	if(rest)
		rest->assign(args + numUsed, args + numNumbers);

	return true;
}

