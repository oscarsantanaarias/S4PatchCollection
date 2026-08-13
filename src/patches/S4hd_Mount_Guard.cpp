#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    // La rutina de mount hace (size - 0x20) sin cota inferior antes del new: un archivo
    // de menos de 0x20 bytes wrappea el unsigned y pide una cantidad enorme, que termina
    // en un throw de C++ que nadie atrapa.
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t S4HD_MOUNT = 0x0049E250;

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
    oMount = (tMount)S4(S4HD_MOUNT);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oMount, hkMount);
    DetourTransactionCommit();
}
