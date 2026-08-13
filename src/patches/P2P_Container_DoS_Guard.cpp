#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <cstdint>
#include "../s4_base.h"

namespace
{
    // Los dos count reads que maneja el peer en el walk del container 20025. Un peer
    // pone el count de 4 bytes en 0x7FFFFFFF y cada iteracion allocatea y retiene un
    // handler en una std::list: OOM o freeze de minutos. Cada sub-entry gasta >=4 bytes,
    // asi que un count legitimo nunca puede pasar de restante/4. El reader tiene el
    // tamano en this+8 y el cursor en this+0x10.
    const uintptr_t COUNT_SITE_1 = 0x00A24132; // FUN_00A24120
    const uintptr_t COUNT_SITE_2 = 0x00A2800F; // FUN_00A27FE0

    typedef unsigned(__fastcall* tRead)(void*, void*, void*, unsigned);
    tRead oRead = nullptr;

    unsigned __fastcall hkReadCount(void* rdr, void* edx, void* dst, unsigned len)
    {
        unsigned r = oRead(rdr, edx, dst, len);
        if (len == 4 && dst)
        {
            unsigned size = *(unsigned*)((char*)rdr + 8);
            unsigned pos  = *(unsigned*)((char*)rdr + 0x10);
            unsigned maxN = size > pos ? (size - pos) / 4 : 0;
            if (*(unsigned*)dst > maxN)
                *(unsigned*)dst = maxN;
        }
        return r;
    }

    bool RepointCall(uintptr_t site, void* target, void** origOut)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        int32_t* rel = (int32_t*)(site + 1);
        void* current = (void*)(site + 5 + *rel);
        if (current == target)
            return true;                  // ya repunteado, no se vuelve a tomar
        *origOut = current;
        DWORD old;
        if (!VirtualProtect(rel, 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *rel = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect(rel, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallP2PContainerDoSGuard()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    RepointCall(S4(COUNT_SITE_1), (void*)hkReadCount, (void**)&oRead);
    RepointCall(S4(COUNT_SITE_2), (void*)hkReadCount, (void**)&oRead);
}
