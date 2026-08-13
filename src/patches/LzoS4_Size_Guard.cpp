#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"
#include <cstdint>

namespace
{
    // Every LZO caller that does `new[](header+1)` (wrap 0xFFFFFFFF->0) and then
    // passes the raw attacker header as the decompress capacity to
    // lzo1x_decompress_safe (0x01b7cd50). The two sites per caller are hooked with
    // the SAME stateless clamp: alloc and capacity each sanitize the header, so
    // they stay consistent. new[] = 0x01b25574 (shared), decompressor = 0x01b7cd50.
    struct Pair { uintptr_t newSite; uintptr_t decSite; };
    const Pair SITES[] = {
        { 0x01BF49DF, 0x01BF4A17 }, // FUN_01bf4730  (.s4 blob)
        { 0x00F6AED7, 0x00F6B04A }, // FUN_00f6a2c0  (generic encrypted-LZO decoder, 64 call-sites)
        { 0x01C165BC, 0x01C165F4 }, // FUN_01c16300  (.s4)
        { 0x01C16EFC, 0x01C16F34 }, // FUN_01c16c40  (.s4)
        { 0x01C1784C, 0x01C17884 }, // FUN_01c17590  (.s4)
        { 0x01C181A6, 0x01C181DE }, // FUN_01c17ef0  (.s4)
        { 0x01C4DB6F, 0x01C4DBA7 }, // FUN_01c4d8a0  (.s4)
        { 0x01B4301B, 0x01B430DB }, // FUN_01b42c50  (.x7 encrypted container)
        // Found by scanning the whole image for E8 -> 0x01b7cd50 instead of trusting the
        // list above: 11 call sites, and these two were uncovered. Same shape as the rest
        // (new[] a few instructions before the decompress call).
        { 0x0164968E, 0x016496B1 }, // FUN_01649620  (216 b)
        { 0x0164A99E, 0x0164A9C1 }, // FUN_0164a930  (216 b, structural twin of the above)
        // The 11th site, 0x019077b9 (FUN_019077a0, 49 b), has no new[] near it: thin
        // wrapper that forwards someone else's buffer, nothing to clamp.
    };

    // ponytail: 128 MiB ceiling on a decompressed blob; raise if a legit resource
    // ever needs more. Bounds both the alloc and the decompress capacity.
    const unsigned MAX_OUT = 0x08000000;

    unsigned Sane(unsigned v) { return v > MAX_OUT ? MAX_OUT : v; }

    typedef void*(__cdecl* tNew)(size_t);
    tNew oNew = nullptr;

    void* __cdecl hkNew(size_t reqSize)
    {
        unsigned hdr = (unsigned)reqSize - 1u;
        return oNew((size_t)Sane(hdr) + 1u);
    }

    typedef int(__cdecl* tDec)(void*, unsigned, void*, unsigned*, void*);
    tDec oDec = nullptr;

    int __cdecl hkDec(void* src, unsigned srclen, void* dst, unsigned* pcap, void* wrk)
    {
        if (pcap)
            *pcap = Sane(*pcap);
        return oDec(src, srclen, dst, pcap, wrk);
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

void InstallLzoS4SizeGuard()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    for (const Pair& p : SITES)
    {
        RepointCall(S4(p.newSite), (void*)hkNew, (void**)&oNew);
        RepointCall(S4(p.decSite), (void*)hkDec, (void**)&oDec);
    }
}
