#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    // Primitiva de lectura del stream: memcpy_s(dst, size, src, size) usa el MISMO size
    // como cota y como cantidad, asi que no protege el destino (el caller pasa un buffer
    // fijo). Su chequeo solo cubre el origen, asi que un size grande con stream
    // suficiente desborda el buffer del caller.
    const uintptr_t READ_PRIM   = 0x006B9220;
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
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    oRead = (tRead)S4(READ_PRIM);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oRead, hkRead);
    DetourTransactionCommit();
}
