#include "ObseExpression.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "Hooks_Script.h"
#include "obse64_common/Log.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>

CommandInfo * GetCommandInfo(u32 opcode);	// Hooks_Script.cpp

// classic OBSE's variable types, as compiled into 'V' tokens
enum
{
	kVarType_Float =	0,
	kVarType_Integer =	1,
	kVarType_String =	2,
	kVarType_Array =	3,
	kVarType_Ref =		4,
};

// classic OBSE's operators, compiled as their number in its OperatorType
enum
{
	kOpType_In =		26,	// iterator <- source, ForEach's
	kOpType_MakePair =	36,	// key::value
};

// what the ambiguous command run last gave, for EvaluateCommand
static ObseExpression::Value s_ambiguousResult;

void ObseExpression::SetAmbiguousResult(const Value & value)
{
	s_ambiguousResult = value;

	// the value alone, not the variable of the function's it was read from
	s_ambiguousResult.isLocalVar = false;
	s_ambiguousResult.varType = 0;
	s_ambiguousResult.varIdx = 0;
}

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

bool ObseExpression::Skip(u32 length)
{
	if(m_data + length > m_end)
	{
		Fail("expression runs past the end of the command");
		return false;
	}

	m_data += length;

	return true;
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
		if(!ReadToken(argEnd, &stack)) return false;
	}

	if(stack.size() != 1)
	{
		Fail("expression left %d values instead of one", int(stack.size()));
		return false;
	}

	*out = stack.back();

	return true;
}

// Reads one token, which ends before end: a value goes on the stack, and an operator takes its
// operands off the stack and leaves its result there.
bool ObseExpression::ReadToken(const u8 * end, std::vector <Value> * stack)
{
	u8 code = 0;
	if(!Read(&code)) return false;

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

			if(m_data + len > end)
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
		{
			// a form from the script's reference list, which may be a ref variable
			u16 refIdx;
			if(!Read(&refIdx)) return false;

			TESForm * form = nullptr;
			if(!ResolveForm(refIdx, &form))
			{
				Fail("ref %d is not in the script's reference list", refIdx);
				return false;
			}

			value.type = Value::kType_Form;
			value.formID = form ? form->refID : 0;
		}
		break;

		case 'G':
		{
			u16 refIdx;
			if(!Read(&refIdx)) return false;

			// the game's own token for a global is the same
			const u8 code[] = { 'G', u8(refIdx), u8(refIdx >> 8) };
			if(!ReadGameNumber(code, &value.number))
			{
				Fail("global %d could not be read", refIdx);
				return false;
			}

			value.type = Value::kType_Number;
		}
		break;

		case 'V':
		{
			u8 varType;
			u16 refIdx, varIdx;
			if(!Read(&varType) || !Read(&refIdx) || !Read(&varIdx)) return false;

			if(varType != kVarType_Float && varType != kVarType_Integer && varType != kVarType_Ref)
			{
				Fail("%s variables are not supported yet",
					varType == kVarType_String ? "string" : (varType == kVarType_Array ? "array" : "unknown"));
				return false;
			}

			// in the game's own tokens: 'f' and the variable's index, after 'r' and a
			// reference's index for a variable of another script (a quest's or a reference's)
			u8 code[6];
			u32 len = 0;
			if(refIdx)
			{
				code[len++] = 'r';
				code[len++] = u8(refIdx);
				code[len++] = u8(refIdx >> 8);
			}
			code[len++] = 'f';
			code[len++] = u8(varIdx);
			code[len++] = u8(varIdx >> 8);

			double number = 0;
			if(!ReadGameNumber(code, &number))
			{
				Fail("variable %d could not be read", varIdx);
				return false;
			}

			if(varType == kVarType_Ref)
			{
				value.type = Value::kType_Form;
				value.formID = FormIDFromValue(number);
			}
			else
			{
				value.type = Value::kType_Number;
				value.number = number;
			}

			value.isLocalVar = !refIdx;
			value.varType = varType;
			value.varIdx = varIdx;
		}
		break;

		default:
			if(code < 'B')
				return ApplyOperator(code, stack);

			Fail("unknown token type %02X", code);
			return false;
	}

	stack->push_back(value);

	return true;
}

// A Call's arguments: classic's version byte, then the function (0: a single token, 1: an
// expression), then the arguments, as ExtractArgs reads them.
bool ObseExpression::ExtractCallArgs(Value * function)
{
	u8 version = 0;
	if(!Read(&version)) return false;

	switch(version)
	{
		case 0:
		{
			std::vector <Value> stack;
			if(!ReadToken(m_end, &stack)) return false;

			if(stack.size() != 1)
			{
				Fail("Call's function is not a value");
				return false;
			}

			*function = stack.back();
		}
		break;

		case 1:
			if(!Evaluate(function)) return false;
			break;

		default:
			Fail("unknown Call version %d", version);
			return false;
	}

	return ExtractArgs();
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
		// as in classic, the form has to be a reference
		TESForm * form = nullptr;
		if(!ResolveForm(refIdx, &form))
		{
			Fail("ref %d is not in the script's reference list", refIdx);
			return false;
		}

		if(!form)
		{
			Fail("%s called on a null reference", cmd->name);
			return false;
		}

		callingObj = DYNAMIC_CAST(form, TESForm, TESObjectREFR);
		if(!callingObj)
		{
			Fail("%s called on a base object (%08X)", cmd->name, form->refID);
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

	// the command reads its arguments from the script data at an offset, as it does when it's a
	// statement, with its opcode four bytes before them
	double cmdResult = 0;
	u32 offset = u32(args - m_scriptData);

	CommandReturnType retnType = GetCommandReturnType(opcode);
	if(retnType == kRetnType_Ambiguous)
		s_ambiguousResult = Value();

	if(!cmd->execute(cmd->params, (const char *)m_scriptData, callingObj, refIdx ? nullptr : m_containingObj,
		m_script, m_locals, &cmdResult, &offset))
	{
		Fail("%s failed", cmd->name);
		return false;
	}

	m_data = args + argsLen - sizeof(argsLen);

	switch(retnType)
	{
		case kRetnType_Default:
			out->type = Value::kType_Number;
			out->number = cmdResult;
			break;

		case kRetnType_Form:
			out->type = Value::kType_Form;
			out->formID = FormIDFromValue(cmdResult);
			break;

		case kRetnType_Ambiguous:
			// what the command gave, or its number if it gave nothing (a function that set no value)
			if(s_ambiguousResult.type != Value::kType_Invalid)
			{
				*out = s_ambiguousResult;
			}
			else
			{
				out->type = Value::kType_Number;
				out->number = cmdResult;
			}
			break;

		default:
			Fail("%s returns a string or an array, which is not supported yet", cmd->name);
			return false;
	}

	return true;
}

bool ObseExpression::ApplyOperator(u8 op, std::vector <Value> * stack)
{
	switch(op)
	{
		case kOpType_MakePair:
		{
			if(stack->size() < 2)
			{
				Fail("'::' without a key and a value");
				return false;
			}

			Value pair;
			pair.type = Value::kType_Pair;
			pair.pair.assign(stack->end() - 2, stack->end());

			stack->resize(stack->size() - 2);
			stack->push_back(pair);
		}
		return true;

		case kOpType_In:
		{
			if(stack->size() < 2)
			{
				Fail("'<-' without an iterator and a source");
				return false;
			}

			const Value & iterator = (*stack)[stack->size() - 2];
			const Value & source = stack->back();

			// classic also loops over arrays and strings, with variables of those types
			if(!iterator.isLocalVar || iterator.varType != kVarType_Ref)
			{
				Fail("ForEach over an array or a string is not supported yet");
				return false;
			}

			if(source.type != Value::kType_Form)
			{
				Fail("ForEach over something other than a container");
				return false;
			}

			Value context;
			context.type = Value::kType_ForEach;
			context.formID = source.formID;
			context.varIdx = iterator.varIdx;

			stack->resize(stack->size() - 2);
			stack->push_back(context);
		}
		return true;
	}

	Fail("operator %d is not supported yet", op);
	return false;
}

// The form in entry refIdx of the script's reference list; false if there is no such entry. A
// ref variable's entry is resolved from the variable's value, as classic does it: the game's own
// lookup keeps the entry's previous form when the variable has been set to 0.
bool ObseExpression::ResolveForm(u16 refIdx, TESForm ** out)
{
	// without locals the game only finds the entry
	auto * entry = (const u8 *)Script_GetResolvedRefVariable(m_script, refIdx, nullptr);
	if(!entry) return false;

	u32 varIdx = *(const u32 *)(entry + 0x18);
	if(varIdx)
	{
		if(!m_locals) return false;

		u32 formID = FormIDFromValue(ScriptLocals_GetVariableValue(m_locals, varIdx, m_script));
		*out = formID ? LookupFormByID(formID) : nullptr;
	}
	else
		*out = *(TESForm * const *)(entry + 0x10);

	return true;
}

// Reads a value written in the game's own argument tokens with the game's reader, so variables
// and globals resolve exactly as they do for vanilla commands.
bool ObseExpression::ReadGameNumber(const u8 * code, double * out)
{
	u32 offset = 0;
	return ExtractNumericArg(out, (const char *)code, &offset, m_thisObj, m_containingObj, m_script, m_locals, true);
}
