#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t S4HD_MOUNT = 0x00F1F220;

    typedef void(__cdecl* tMount)(char*, unsigned*);
    tMount oMount = nullptr;

    void __cdecl hkMount(char* path, unsigned* outSize)
    {
        if (outSize)
            *outSize = 0;
        __try
        {
            oMount(path, outSize);
        }
        __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
        }
    }
}

void InstallS4hdMountGuard()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oMount = (tMount)S4(S4HD_MOUNT);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oMount, hkMount);
    DetourTransactionCommit();
}
