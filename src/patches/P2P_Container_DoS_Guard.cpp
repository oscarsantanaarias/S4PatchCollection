#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"
#include <cstdint>

namespace
{
    // The two peer-controlled count reads that drive the 20025 container walk:
    // FUN_014DCD40 sub-msg count (0x014dcd52) and FUN_014E0DD0 entry count
    // (0x014e0dff). A peer sets the 4-byte count to 0x7FFFFFFF; each iteration
    // heap-allocs + retains a handler in a std::list -> OOM crash / minutes-long
    // freeze. Each sub-entry consumes >=4 bytes, so a legit count can never
    // exceed remaining/4 -> clamping to that is lossless and kills the amplification.
    const uintptr_t COUNT_SITE_1 = 0x014DCD52; // FUN_014DCD40
    const uintptr_t COUNT_SITE_2 = 0x014E0DFF; // FUN_014E0DD0

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
