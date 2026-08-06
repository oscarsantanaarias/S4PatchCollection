#include <windows.h>
#include "detours.h"

namespace
{
    const uintptr_t READ_PRIM   = 0x01155F70;
    const unsigned  BUFFER_SIZE = 256;

    typedef char(__fastcall* tRead)(void*, void*, void*, unsigned);
    tRead oRead = (tRead)READ_PRIM;

    char __fastcall hkRead(void* thisptr, void* edx, void* dst, unsigned size)
    {
        if (size > BUFFER_SIZE)
            size = BUFFER_SIZE - 1;
        return oRead(thisptr, edx, dst, size);
    }
}

void InstallFix_C03_ArcadeOverflow()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oRead, hkRead);
    DetourTransactionCommit();
}
