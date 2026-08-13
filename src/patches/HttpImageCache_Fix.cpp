#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include "../s4_base.h"

namespace
{
    // El cache de texturas HTTP crece sin tope: cada una entra y ninguna se saca. Se
    // corta en CACHE_CAP devolviendo un iterador nulo desde el insert del map.
    const uintptr_t INSERT_FN    = 0x006C0D30;
    const uintptr_t CALL_SITE    = 0x0097B866;
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
    oInsert = (tInsert)S4(INSERT_FN);
    RepointCall(S4(CALL_SITE), (void*)guardInsert);
}
