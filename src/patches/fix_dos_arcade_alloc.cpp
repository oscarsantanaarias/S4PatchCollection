#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    const uintptr_t ARCADE_ALLOC = 0x01155B90; // CArcadeStream_AllocOut_attacker_size
    const unsigned  STREAM_MAX   = 0x400000;   // ponytail: 4 MB ceiling; real arcade streams are a few KB, upgrade path = tighten if a true max is ever measured

    typedef int(__fastcall* tAlloc)(void*, void*, unsigned);
    tAlloc oAlloc = (tAlloc)ARCADE_ALLOC;

    int __fastcall hkAlloc(void* thisptr, void* edx, unsigned a2)
    {
        if (a2 > STREAM_MAX)
            a2 = STREAM_MAX; // a2 is both the alloc size and the recorded stream end, so clamping keeps them consistent - no OOB, just a smaller-than-declared stream
        return oAlloc(thisptr, edx, a2);
    }
}

void InstallFix_DoS_ArcadeAlloc()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oAlloc, hkAlloc);
    DetourTransactionCommit();
}
