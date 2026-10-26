#pragma once

#include "GameScript.h"
#include <string>
#include <vector>

// A first slice of classic OBSE's expression evaluator. Classic OBSE compiles the arguments of
// commands such as TestExpr, PrintD and let in its own format: an argument count, then for
// each argument a length and a postfix token stream. This evaluates numbers, strings, forms,
// globals, number and ref variables, command calls and the pair operator (key::value); other
// operators and string or array variables are reported as unsupported and fail the argument.
class ObseExpression
{
public:
	struct Value
	{
		enum Type
		{
			kType_Invalid,
			kType_Number,
			kType_String,
			kType_Form,
			kType_Pair,
		};

		Type		type = kType_Invalid;
		double		number = 0;
		u32			formID = 0;
		std::string	string;
		std::vector <Value>	pair;	// a pair's key and value
	};

	ObseExpression(COMMAND_ARGS);

	// evaluates every argument; false on the first error
	bool	ExtractArgs();

	// a Call's: the function, then its arguments
	bool	ExtractCallArgs(Value * function);

	u32				NumArgs() const { return u32(m_args.size()); }
	const Value &	Arg(u32 idx) const { return m_args[idx]; }

private:
	bool	Evaluate(Value * out);
	bool	ReadToken(const u8 * end, std::vector <Value> * stack);
	bool	EvaluateCommand(Value * out);
	bool	ApplyOperator(u8 op, std::vector <Value> * stack);
	bool	ResolveForm(u16 refIdx, TESForm ** out);
	bool	ReadGameNumber(const u8 * code, double * out);

	template <typename T>
	bool	Read(T * out);

	void	Fail(const char * fmt, ...);

	const u8		* m_scriptData;
	const u8		* m_data;
	const u8		* m_end;
	u32				* m_opcodeOffsetPtr;
	TESObjectREFR	* m_thisObj;
	TESObjectREFR	* m_containingObj;
	Script			* m_script;
	ScriptLocals	* m_locals;

	std::vector <Value>	m_args;
};
