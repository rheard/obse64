#include "Commands_Classic.h"
#include "FunctionScripts.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameRTTI.h"
#include "InventoryReference.h"
#include "ObseExpression.h"
#include "ParamInfos.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Log.h"
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Classic OBSE's Label and Goto (SaveIP and RestoreIP), and its loops: ForEach, over the stacks of
// items in a container, and Loop, Break and Continue. ForEach's argument is classic's expression
// `iterator <- container`, after the offset of the statement past the loop's Loop, which classic's
// compiler writes there once it reaches Loop. The loops jump the way Goto does, putting back the
// if/else nesting the loop started with, in the runner running the script, so they work as
// statements of their own in any script, a function Call runs included. While, whose condition
// needs the evaluator's operators, isn't ported yet.
//
// The iterator holds an inventory reference for each stack in turn (InventoryReference.cpp), made
// when the loop starts and destroyed when it ends, as in classic, and is 0 after the loop. A stack
// that's gone by the time the loop comes to it (the script removed it) is passed over.

// the runner Run's callers use, unless it's busy with another script (0x0694D580)
static RelocPtr <ScriptRunner *> g_scriptRunner(0x094904E8);

// The runners running a script on this thread, innermost last. Run's caller (0x0694D580) gives it
// the main runner, or a new one when that's busy, as for a function a script calls or a script
// that an Activate starts, and both calls (0x0694D6A8 and 0x0694D645) go through a hook that notes
// the runner while it runs. Run's last five arguments, on the stack, are passed on as they are.
typedef u64 (* _ScriptRunner_Run)(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9);
static RelocAddr <_ScriptRunner_Run> ScriptRunner_Run(0x0694D6D0);
static RelocAddr <uintptr_t> kRun_MainRunnerCall(0x0694D6A8);
static RelocAddr <uintptr_t> kRun_NewRunnerCall(0x0694D645);

static thread_local std::vector <ScriptRunner *> s_runners;
static bool s_runnersNoted = false;

static u64 ScriptRunner_Run_Hook(ScriptRunner * runner, void * arg2, void * arg3, void * arg4, u64 arg5,
	u64 arg6, u64 arg7, u64 arg8, u64 arg9)
{
	s_runners.push_back(runner);
	u64 result = ScriptRunner_Run(runner, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
	s_runners.pop_back();

	return result;
}

void InstallRunnerHooks()
{
	uintptr_t run = ScriptRunner_Run.getUIntPtr();
	if(!IsCallTo(kRun_MainRunnerCall.getUIntPtr(), run) || !IsCallTo(kRun_NewRunnerCall.getUIntPtr(), run))
	{
		_ERROR("the script runner's calls aren't where expected; Goto and loops will work only in scripts the main runner runs");
		return;
	}

	g_branchTrampoline.write5Call(kRun_MainRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	g_branchTrampoline.write5Call(kRun_NewRunnerCall.getUIntPtr(), uintptr_t(ScriptRunner_Run_Hook));
	s_runnersNoted = true;
}

// Run moves to the next statement by adding the statement's length to its arguments' offset. It
// passes the length to 0x0694C810 by pointer, as the fifth argument, which a command can change to
// jump, as classic's Goto did. 0x0694C810 calls commands from 0x0694D179 with opcodeOffsetPtr at its
// rsp + 0x40, and its fifth argument is at rsp + 0x800 (eight pushes, 0x798 of locals, the return
// address and four home slots).
static RelocAddr <uintptr_t> kStatementCommandReturn(0x0694D17C);
static const u32 kNextStatementLengthOffset = 0x800 - 0x40;

// where Label was, and the nesting there, by script and label
struct SavedIP
{
	u32	ip;
	s32	ifDepth;
	u32	ifStack[10];
};

static std::map <std::pair <Script *, u32>, SavedIP> s_savedIPs;

bool IsStatementCall(void * returnAddress)
{
	return uintptr_t(returnAddress) == kStatementCommandReturn.getUIntPtr();
}

// The runner running this statement, and Run's length for it, which the command can change to
// jump: the next statement is at argsOffset + *length. False unless the command is a statement of
// its own, not part of an expression, run by the innermost runner running, with this script and
// these locals. returnAddress is the command's _ReturnAddress().
static bool GetStatementState(void * returnAddress, Script * script, ScriptLocals * locals, const char * scriptData,
	u32 argsOffset, u32 * opcodeOffsetPtr, ScriptRunner ** runner, s32 ** length)
{
	if(!IsStatementCall(returnAddress)) return false;

	// The innermost runner, or without the hooks the main one. The main runner can be running this
	// script while another runner runs it too, inside that run (a function calling itself, a
	// script activating another reference with the same script), and putting its nesting back
	// there would leave the inner runner's to grow past its ten levels: so the locals must match.
	if(s_runnersNoted)
		*runner = s_runners.empty() ? nullptr : s_runners.back();
	else
		*runner = *g_scriptRunner;

	if(!*runner || (*runner)->script != script || (*runner)->locals != locals) return false;

	// the length Run read, which is just before the arguments
	*length = *(s32 **)((u8 *)opcodeOffsetPtr + kNextStatementLengthOffset);
	return *length && **length == *(const s16 *)(scriptData + argsOffset - 2);
}

static void ReportUnfollowedJump(const char * name, Script * script)
{
	static bool s_reported = false;
	if(!s_reported)
	{
		s_reported = true;
		_MESSAGE("%s in script %08X ignored: it only works as a statement of its own",
			name, script ? reinterpret_cast <TESForm *>(script)->refID : 0);
	}
}

static bool Cmd_SaveIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Label", script);
		return true;
	}

	SavedIP & saved = s_savedIPs[{ script, label }];
	saved.ip = argsOffset + *length;
	saved.ifDepth = runner->ifDepth;
	memcpy(saved.ifStack, runner->ifStack, sizeof(saved.ifStack));

	return true;
}

static bool Cmd_RestoreIP_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	u32 label = 0;
	if(!ExtractArgs(EXTRACT_ARGS, &label)) return true;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportUnfollowedJump("Goto", script);
		return true;
	}

	// classic jumped to the start of the script for a label it hadn't seen; do nothing instead
	auto saved = s_savedIPs.find({ script, label });
	if(saved == s_savedIPs.end()) return true;

	*length = s32(saved->second.ip) - s32(argsOffset);
	runner->ifDepth = saved->second.ifDepth;
	memcpy(runner->ifStack, saved->second.ifStack, sizeof(runner->ifStack));

	return true;
}

CommandInfo kCommandInfo_SaveIP =
{
	"SaveIP", "",
	0,
	"marks a place in the script for RestoreIP (Goto) to jump back to",
	0,
	1, kParams_OneOptionalInt,
	Cmd_SaveIP_Execute
};

CommandInfo kCommandInfo_RestoreIP =
{
	"RestoreIP", "",
	0,
	"jumps back to the place SaveIP (Label) marked",
	0,
	1, kParams_OneOptionalInt,
	Cmd_RestoreIP_Execute
};

enum
{
	kPlayerBaseID = 0x07,
	kPlayerRefID = 0x14,
};

struct ForEachLoop
{
	Script			* script;
	ScriptLocals	* locals;
	u32				forEachOffset;	// the ForEach statement's arguments
	u32				startIP;		// the first statement in the loop
	u32				endIP;			// the statement after its Loop
	s32				ifDepth;		// the nesting at ForEach
	u32				ifStack[10];

	u32				varIdx;			// the iterator, a ref variable
	TESObjectREFR	* ref = nullptr;	// the inventory reference it holds
	std::vector <InventoryStack>	stacks;
	u32				next = 0;		// the stack it moves to next
};

// innermost last; one a script left by returning inside it stays until it's found unfinished
static std::vector <std::unique_ptr <ForEachLoop>> s_loops;

static void ReportLoopError(const char * name, Script * script, const char * what)
{
	static std::set <std::string> s_reported;

	// Script derives from TESForm
	u32 scriptID = script ? reinterpret_cast <TESForm *>(script)->refID : 0;

	char key[256];
	sprintf_s(key, "%s %08X %s", name, scriptID, what);
	if(s_reported.insert(key).second)
		_MESSAGE("%s in script %08X stopped the script: %s", name, scriptID, what);
}

// ends a loop, with the iterator at 0 unless the script may be gone
static void EndLoop(ForEachLoop * loop, bool clearIterator)
{
	if(clearIterator)
		SetScriptVariable(loop->locals, loop->varIdx, 0);

	if(loop->ref)
	{
		DestroyInventoryRef(loop->ref);
		loop->ref = nullptr;
	}
}

void ResetLoops()
{
	for(auto & loop : s_loops)
		EndLoop(loop.get(), false);

	s_loops.clear();
}

// points the iterator at the loop's next stack that's still there; false when there are no more
static bool NextStack(ForEachLoop * loop)
{
	while(loop->next < loop->stacks.size())
	{
		const InventoryStack & stack = loop->stacks[loop->next++];
		if(!IsInventoryStackValid(stack)) continue;

		if(!loop->ref)
		{
			loop->ref = CreateInventoryRef();
			if(!loop->ref) return false;
		}

		SetInventoryRef(loop->ref, stack);
		SetScriptVariable(loop->locals, loop->varIdx, loop->ref->refID);

		return true;
	}

	return false;
}

// the innermost loop of this script and locals; loops left inside it by scripts that returned
// in them go
static ForEachLoop * FindLoop(Script * script, ScriptLocals * locals)
{
	while(!s_loops.empty())
	{
		ForEachLoop * loop = s_loops.back().get();
		if(loop->script == script && loop->locals == locals)
			return loop;

		EndLoop(loop, false);
		s_loops.pop_back();
	}

	return nullptr;
}

static void JumpTo(ScriptRunner * runner, s32 * length, u32 argsOffset, u32 offset, const ForEachLoop * loop)
{
	runner->ifDepth = loop->ifDepth;
	memcpy(runner->ifStack, loop->ifStack, sizeof(runner->ifStack));

	*length = s32(offset) - s32(argsOffset);
}

static bool Cmd_ForEach_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportLoopError("ForEach", script, "loops only work as statements of their own");
		return false;
	}

	u32 endIP = *(const u32 *)(scriptData + argsOffset);

	ObseExpression eval(PASS_COMMAND_ARGS);
	if(!eval.Skip(sizeof(endIP)) || !eval.ExtractArgs() || eval.NumArgs() != 1 ||
		eval.Arg(0).type != ObseExpression::Value::kType_ForEach)
	{
		ReportLoopError("ForEach", script, "its expression isn't a loop over a container");
		return false;
	}

	const auto & context = eval.Arg(0);

	// the player's base form stands for the player, as in classic
	TESForm * source = LookupFormByID(context.formID == kPlayerBaseID ? kPlayerRefID : context.formID);
	TESObjectREFR * container = source ? DYNAMIC_CAST(source, TESForm, TESObjectREFR) : nullptr;
	if(!container)
	{
		ReportLoopError("ForEach", script, "its source isn't a reference");
		return false;
	}

	// this ForEach starting again ends a run of it the script left unfinished, and what's inside it
	for(size_t i = 0; i < s_loops.size(); i++)
	{
		const ForEachLoop * loop = s_loops[i].get();
		if(loop->script == script && loop->locals == locals && loop->forEachOffset == argsOffset)
		{
			while(s_loops.size() > i)
			{
				EndLoop(s_loops.back().get(), false);
				s_loops.pop_back();
			}
			break;
		}
	}

	auto loop = std::make_unique <ForEachLoop>();
	loop->script = script;
	loop->locals = locals;
	loop->forEachOffset = argsOffset;
	loop->startIP = argsOffset + *length;
	loop->endIP = endIP;
	loop->ifDepth = runner->ifDepth;
	memcpy(loop->ifStack, runner->ifStack, sizeof(loop->ifStack));
	loop->varIdx = context.varIdx;

	GetInventoryStacks(container, &loop->stacks);

	if(!NextStack(loop.get()))
	{
		// nothing to loop over: on past Loop
		EndLoop(loop.get(), true);
		JumpTo(runner, length, argsOffset, endIP, loop.get());
		return true;
	}

	s_loops.push_back(std::move(loop));

	return true;
}

// Loop and Continue: on to the loop's next stack, back at its first statement, or past its Loop
// when there are none left
static bool NextIteration(const char * name, COMMAND_ARGS, void * returnAddress)
{
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportLoopError(name, script, "loops only work as statements of their own");
		return false;
	}

	ForEachLoop * loop = FindLoop(script, locals);
	if(!loop)
	{
		ReportLoopError(name, script, "it isn't in a loop");
		return false;
	}

	if(NextStack(loop))
	{
		JumpTo(runner, length, argsOffset, loop->startIP, loop);
		return true;
	}

	JumpTo(runner, length, argsOffset, loop->endIP, loop);
	EndLoop(loop, true);
	s_loops.pop_back();

	return true;
}

static bool Cmd_Loop_Execute(COMMAND_ARGS)
{
	return NextIteration("Loop", PASS_COMMAND_ARGS, _ReturnAddress());
}

static bool Cmd_Continue_Execute(COMMAND_ARGS)
{
	return NextIteration("Continue", PASS_COMMAND_ARGS, _ReturnAddress());
}

static bool Cmd_Break_Execute(COMMAND_ARGS)
{
	void * returnAddress = _ReturnAddress();
	u32 argsOffset = *opcodeOffsetPtr;

	*result = 0;

	ScriptRunner * runner = nullptr;
	s32 * length = nullptr;
	if(!GetStatementState(returnAddress, script, locals, scriptData, argsOffset, opcodeOffsetPtr, &runner, &length))
	{
		ReportLoopError("Break", script, "loops only work as statements of their own");
		return false;
	}

	ForEachLoop * loop = FindLoop(script, locals);
	if(!loop)
	{
		ReportLoopError("Break", script, "it isn't in a loop");
		return false;
	}

	JumpTo(runner, length, argsOffset, loop->endIP, loop);
	EndLoop(loop, true);
	s_loops.pop_back();

	return true;
}

static ParamInfo kParams_ForEach[1] =
{
	{ "ForEach expression", kParamType_String, 0 },
};

CommandInfo kCommandInfo_ForEach =
{
	"ForEach", "",
	0,
	"iterates over the items in a container",
	0,
	1, kParams_ForEach,
	Cmd_ForEach_Execute,
	Cmd_Expression_Parse
};

CommandInfo kCommandInfo_Loop =
{
	"Loop", "",
	0,
	"marks the end of a ForEach loop",
	0,
	0, nullptr,
	Cmd_Loop_Execute
};

CommandInfo kCommandInfo_Continue =
{
	"Continue", "",
	0,
	"returns control to the top of a loop",
	0,
	0, nullptr,
	Cmd_Continue_Execute
};

CommandInfo kCommandInfo_Break =
{
	"Break", "",
	0,
	"exits a loop",
	0,
	0, nullptr,
	Cmd_Break_Execute
};
