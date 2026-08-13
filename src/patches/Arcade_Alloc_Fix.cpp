#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"
#include "detours.h"

namespace
{
    const uintptr_t ARCADE_ALLOC = 0x006B8E40;
    const unsigned  STREAM_MAX   = 0x400000;

    typedef int(__fastcall* tAlloc)(void*, void*, unsigned);
    tAlloc oAlloc = nullptr;

    int __fastcall hkAlloc(void* thisptr, void* edx, unsigned a2)
    {
        if (a2 > STREAM_MAX)
            a2 = STREAM_MAX;
        return oAlloc(thisptr, edx, a2);
    }
}

void InstallArcadeAllocFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    oAlloc = (tAlloc)S4(ARCADE_ALLOC);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oAlloc, hkAlloc);
    DetourTransactionCommit();
}
