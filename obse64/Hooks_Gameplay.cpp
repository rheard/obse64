#include "Hooks_Gameplay.h"
#include "Commands_Classic.h"
#include "obse64_common/BranchTrampoline.h"
#include "obse64_common/Relocation.h"

RelocAddr <uintptr_t> OblivionThread_Target(0x065D2070 + 0x1208);
RelocAddr <uintptr_t> UnrealGameThread_Target(0x03907660 + 0x53);

// once each pass of the Oblivion thread's main loop, before the frame is run
void OblivionThreadHook(const char * dbgStr)
{
	ProcessInputEvents();
}

void UnrealGameThreadHook()
{

}

void Hooks_Gameplay_Apply()
{
	// replace call to stubbed debug logging function being passed "Oblivion Main loop"
	g_branchTrampoline.write5Call(OblivionThread_Target.getUIntPtr(), uintptr_t(OblivionThreadHook));

	// replace call to stubbed hitch detection function
	g_branchTrampoline.write5Call(UnrealGameThread_Target.getUIntPtr(), uintptr_t(UnrealGameThreadHook));
}
