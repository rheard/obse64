#include "ObseExpression.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "Hooks_Script.h"
#include "obse64_common/Log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>

CommandInfo * GetCommandInfo(u32 opcode);	// Hooks_Script.cpp

ObseExpression::ObseExpression(COMMAND_ARGS) :
	m_scriptData((const u8 *)scriptData),
	m_opcodeOffsetPtr(opcodeOffsetPtr),
	m_thisObj(thisObj),
	m_containingObj(containingObj),
	m_script(script),
	m_locals(locals)
{
	m_data = m_scriptData + *opcodeOffsetPtr;

	// the u16 before a command's arguments is their length, whether the command is a
	// statement or called from inside an expression
	m_end = m_data + *(const u16 *)(m_data - 2);
}

template <typename T>
bool ObseExpression::Read(T * out)
{
	if(m_data + sizeof(T) > m_end)
	{
		Fail("expression runs past the end of the command");
		return false;
	}

	memcpy(out, m_data, sizeof(T));
	m_data += sizeof(T);

	return true;
}

void ObseExpression::Fail(const char * fmt, ...)
{
	static std::set <std::string> s_reported;

	char buf[512];

	va_list args;
	va_start(args, fmt);
	vsprintf_s(buf, fmt, args);
	va_end(args);

	// Script derives from TESForm
	u32 scriptID = m_script ? reinterpret_cast <TESForm *>(m_script)->refID : 0;

	std::string key = std::to_string(scriptID) + buf;
	if(s_reported.insert(key).second)
		_MESSAGE("OBSE expression in script %08X: %s", scriptID, buf);
}

bool ObseExpression::ExtractArgs()
{
	u8 numArgs = 0;
	if(!Read(&numArgs)) return false;

	for(u32 i = 0; i < numArgs; i++)
	{
		Value arg;
		if(!Evaluate(&arg)) return false;

		m_args.push_back(arg);
	}

	// leave the offset after the arguments, as classic OBSE does
	*m_opcodeOffsetPtr = u32(m_data - m_scriptData);

	return true;
}

bool ObseExpression::Evaluate(Value * out)
{
	const u8 * argStart = m_data;

	u16 argLen = 0;
	if(!Read(&argLen)) return false;

	// the length counts itself
	const u8 * argEnd = argStart + argLen;
	if(argLen < sizeof(argLen) || argEnd > m_end)
	{
		Fail("argument runs past the end of the command");
		return false;
	}

	std::vector <Value> stack;

	while(m_data < argEnd)
	{
		u8 code = 0;
		Read(&code);

		Value value;

		switch(code)
		{
			case 'B':
			case 'b':
			{
				u8 num;
				if(!Read(&num)) return false;
				value.type = Value::kType_Number;
				value.number = num;
			}
			break;

			case 'I':
			case 'i':
			{
				u16 num;
				if(!Read(&num)) return false;
				value.type = Value::kType_Number;
				value.number = num;
			}
			break;

			case 'L':
			case 'l':
			{
				u32 num;
				if(!Read(&num)) return false;
				value.type = Value::kType_Number;
				value.number = num;
			}
			break;

			case 'Z':
				if(!Read(&value.number)) return false;
				value.type = Value::kType_Number;
				break;

			case 'S':
			{
				u16 len;
				if(!Read(&len)) return false;

				if(m_data + len > argEnd)
				{
					Fail("string runs past the end of its argument");
					return false;
				}

				value.type = Value::kType_String;
				value.string.assign((const char *)m_data, len);
				m_data += len;
			}
			break;

			case 'X':
				if(!EvaluateCommand(&value)) return false;
				break;

			case 'R':
				Fail("form tokens are not supported yet");
				return false;

			case 'G':
				Fail("global variable tokens are not supported yet");
				return false;

			case 'V':
				Fail("variable tokens are not supported yet");
				return false;

			default:
				if(code < 'B')
					Fail("operator %d is not supported yet", code);
				else
					Fail("unknown token type %02X", code);
				return false;
		}

		stack.push_back(value);
	}

	if(stack.size() != 1)
	{
		Fail("expression left %d values instead of one", int(stack.size()));
		return false;
	}

	*out = stack.back();

	return true;
}

bool ObseExpression::EvaluateCommand(Value * out)
{
	u16 refIdx = 0;
	u16 opcode = 0;
	u16 argsLen = 0;
	if(!Read(&refIdx) || !Read(&opcode) || !Read(&argsLen)) return false;

	CommandInfo * cmd = GetCommandInfo(opcode);
	if(!cmd || !cmd->execute)
	{
		Fail("unknown command %04X", opcode);
		return false;
	}

	TESObjectREFR * callingObj = m_thisObj;
	if(refIdx)
	{
		callingObj = ResolveRef(refIdx);
		if(!callingObj)
		{
			Fail("%s called on a null reference", cmd->name);
			return false;
		}
	}

	if(cmd->needsParent && !callingObj)
	{
		Fail("%s called without a reference", cmd->name);
		return false;
	}

	// the length counts itself
	const u8 * args = m_data;
	if(argsLen < sizeof(argsLen) || args + argsLen - sizeof(argsLen) > m_end)
	{
		Fail("arguments of %s run past the end of the command", cmd->name);
		return false;
	}

	double cmdResult = 0;
	u32 bytesRead = 0;

	if(!cmd->execute(cmd->params, (const char *)args, callingObj, refIdx ? nullptr : m_containingObj,
		m_script, m_locals, &cmdResult, &bytesRead))
	{
		Fail("%s failed", cmd->name);
		return false;
	}

	m_data = args + argsLen - sizeof(argsLen);

	switch(GetCommandReturnType(opcode))
	{
		case kRetnType_Default:
			out->type = Value::kType_Number;
			out->number = cmdResult;
			break;

		case kRetnType_Form:
			out->type = Value::kType_Form;
			out->formID = *(u32 *)&cmdResult;
			break;

		default:
			Fail("%s returns a string or an array, which is not supported yet", cmd->name);
			return false;
	}

	return true;
}

// The game resolves the reference, the way it resolves any reference passed to a command:
// as an object-ref argument naming the script's refIdx'th reference.
TESObjectREFR * ObseExpression::ResolveRef(u16 refIdx)
{
	static ParamInfo kParams_OneObjectRef[1] =
	{
		{	"reference", kParamType_ObjectRef, 0 },
	};

	const u8 args[] = { 1, 0, 'r', u8(refIdx), u8(refIdx >> 8) };
	u32 offset = 0;
	TESObjectREFR * refr = nullptr;

	if(!::ExtractArgs(kParams_OneObjectRef, (const char *)args, &offset, m_thisObj, m_containingObj,
		m_script, m_locals, &refr))
		return nullptr;

	return refr;
}
