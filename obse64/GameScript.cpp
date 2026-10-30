#include "GameScript.h"
#include <cstdarg>
#include <cstring>

RelocAddr <ParseFunction> DefaultParseFunction(0x0694B810);
RelocAddr <_ExtractArgs> ExtractArgs(0x06630750);
RelocAddr <_ExtractNumericArg> ExtractNumericArg(0x0662F330);
RelocAddr <_ScriptLocals_GetVariableValue> ScriptLocals_GetVariableValue(0x0662F660);
RelocAddr <_Script_GetResolvedRefVariable> Script_GetResolvedRefVariable(0x0662F140);

bool ExtractArgsAnyForm(const ParamInfo * paramInfo, const char * scriptData, u32 * opcodeOffsetPtr,
	TESObjectREFR * thisObj, TESObjectREFR * containingObj, Script * script, ScriptLocals * locals, ...)
{
	va_list args;
	va_start(args, locals);

	// how many arguments the script gives, then each, as its parameter is read
	s16 numArgs = *(const s16 *)(scriptData + *opcodeOffsetPtr);
	*opcodeOffsetPtr += 2;

	bool ok = true;
	for(s32 i = 0; ok && i < numArgs; i++)
	{
		switch(paramInfo[i].typeID)
		{
			case kParamType_String:
			{
				char * out = va_arg(args, char *);
				u16 len = *(const u16 *)(scriptData + *opcodeOffsetPtr);
				memcpy(out, scriptData + *opcodeOffsetPtr + 2, len);
				out[len] = 0;
				*opcodeOffsetPtr += 2 + len;
			}
			break;

			case kParamType_Integer:
			case kParamType_QuestStage:
			case kParamType_Float:
			{
				void * out = va_arg(args, void *);
				double value = 0;
				ok = ExtractNumericArg(&value, scriptData, opcodeOffsetPtr, thisObj, containingObj, script, locals, true);

				if(paramInfo[i].typeID == kParamType_Float)
					*(float *)out = float(value);
				else
					*(s32 *)out = s32(value);
			}
			break;

			case kParamType_ActorValue:
			case kParamType_AnimationGroup:
			case kParamType_Sex:
			case kParamType_CrimeType:
				*va_arg(args, s32 *) = *(const s16 *)(scriptData + *opcodeOffsetPtr);
				*opcodeOffsetPtr += 2;
				break;

			default:
			{
				// 'r' and the form's entry in the script's reference list, which the game fills
				// from a ref variable's value, as classic's ExtractArgsEx resolved it
				TESForm ** out = va_arg(args, TESForm **);
				*out = nullptr;

				if(scriptData[*opcodeOffsetPtr] == 'r')
				{
					u16 refIdx = *(const u16 *)(scriptData + *opcodeOffsetPtr + 1);
					*opcodeOffsetPtr += 3;

					if(auto * entry = (const u8 *)Script_GetResolvedRefVariable(script, refIdx, locals))
						*out = *(TESForm * const *)(entry + 0x10);
				}

				ok = *out != nullptr;
			}
			break;
		}
	}

	va_end(args);

	return ok;
}

// wrap this to give it a static address
bool Cmd_Default_Parse(u32 numParams, const ParamInfo * paramInfo, ScriptLineBuffer * lineBuf, ScriptBuffer * scriptBuf)
{
	return DefaultParseFunction(numParams, paramInfo, lineBuf, scriptBuf);
}

RelocPtr <CommandInfo>	g_firstScriptCommand(0x08FBA4A0);
RelocPtr <CommandInfo>	g_firstConsoleCommand(0x08FC1890);
