#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    const uintptr_t ARCADE_ALLOC = 0x01155B90;
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
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oAlloc = (tAlloc)S4(ARCADE_ALLOC);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oAlloc, hkAlloc);
    DetourTransactionCommit();
}
