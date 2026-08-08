#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t S4HD_MOUNT = 0x00F1F220;

    typedef void(__cdecl* tMount)(char*, unsigned*);
    tMount oMount = (tMount)S4HD_MOUNT;

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
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oMount, hkMount);
    DetourTransactionCommit();
}
