#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"
#include <cstdint>

namespace
{
    const uintptr_t REPARSE_FN = 0x01019ED0;
    const uintptr_t CALL_SITE  = 0x00929BCA;

    typedef void(__fastcall* tReparse)(void* thisTbl, void* edx, void* Str);
    tReparse oReparse = nullptr;
    volatile long g_tipLoaded = 0;

    void __fastcall guardReparse(void* thisTbl, void* edx, void* Str)
    {
        if (InterlockedCompareExchange(&g_tipLoaded, 1, 0) == 0)
            oReparse(thisTbl, edx, Str);
    }

    bool RepointCall(uintptr_t site, void* target)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        DWORD old;
        if (!VirtualProtect((void*)(site + 1), 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect((void*)(site + 1), 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallLoadingTipCache()
{
    oReparse = (tReparse)S4(REPARSE_FN);
    RepointCall(S4(CALL_SITE), (void*)guardReparse);
}
