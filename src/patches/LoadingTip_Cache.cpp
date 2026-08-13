#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "../s4_base.h"

namespace
{
    const uintptr_t REPARSE_FN = 0x01B5A210;
    const uintptr_t CALL_SITE  = 0x013DA462;

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
        if ((void*)(site + 5 + *(int32_t*)(site + 1)) == target)
            return true;                  // ya repunteado
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
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oReparse = (tReparse)S4(REPARSE_FN);

    RepointCall(S4(CALL_SITE), (void*)guardReparse);
}
