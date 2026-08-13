#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    const uintptr_t READ_PRIM   = 0x01155F70;
    const unsigned  BUFFER_SIZE = 256;

    typedef char(__fastcall* tRead)(void*, void*, void*, unsigned);
    tRead oRead = nullptr;

    char __fastcall hkRead(void* thisptr, void* edx, void* dst, unsigned size)
    {
        if (size > BUFFER_SIZE)
            size = BUFFER_SIZE - 1;
        return oRead(thisptr, edx, dst, size);
    }
}

void InstallArcadeStackOverflowFix()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oRead = (tRead)S4(READ_PRIM);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oRead, hkRead);
    DetourTransactionCommit();
}
