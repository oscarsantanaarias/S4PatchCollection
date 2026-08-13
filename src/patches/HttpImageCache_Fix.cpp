#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "../s4_base.h"

namespace
{
    const uintptr_t INSERT_FN    = 0x0142CA80;
    const uintptr_t CALL_SITE    = 0x0142C776;
    const long      CACHE_CAP    = 1024;

    typedef void* (__fastcall* tInsert)(void* thisMap, void* edx, void* out, int a2, void* a3, void* a4);
    tInsert oInsert = nullptr;
    volatile long g_count = 0;

    void* __fastcall guardInsert(void* thisMap, void* edx, void* out, int a2, void* a3, void* a4)
    {
        if (g_count < CACHE_CAP)
        {
            InterlockedIncrement(&g_count);
            return oInsert(thisMap, edx, out, a2, a3, a4);
        }
        *(int*)out = 0;
        *((int*)out + 1) = 0;
        return out;
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

void InstallHttpImageCacheFix()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oInsert = (tInsert)S4(INSERT_FN);

    RepointCall(S4(CALL_SITE), (void*)guardInsert);
}
