#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <cstdint>

namespace
{
    // Cada caller LZO que hace new[](header+1) (con wrap 0xFFFFFFFF->0) y despues le
    // pasa el header crudo del atacante como capacidad a lzo1x_decompress_safe
    // (0x00B61BB0). Los dos sites de cada caller llevan el MISMO clamp sin estado: el
    // alloc y la capacidad sanitizan el header igual, asi que no se desincronizan.
    // new[] = 0x00B59A16 (thunk a la IAT), decompresor = 0x00B61BB0.
    //
    // La lista sale de los xrefs al decompresor: 8 callers, 6 con su new[] pegado (los
    // vulnerables). Los otros dos no entran: 0x00A0EA20 es un wrapper que no allocatea
    // y 0x008B8290 pide new(size) sin el +1, o sea alloc == capacidad.
    struct Pair { uintptr_t newSite; uintptr_t decSite; };
    const Pair SITES[] = {
        { 0x00B888CD, 0x00B8897B }, // decoder encriptado ^0xFE292513
        { 0x00B8BD2A, 0x00B8BDF9 }, // decoder encriptado ^0xFE292513
        { 0x00BFF79F, 0x00BFF7D7 },
        { 0x00C0028F, 0x00C002C7 },
        { 0x00C00F5F, 0x00C00F97 },
        { 0x00C018DF, 0x00C01917 },
    };

    // Techo de 128 MiB para un blob descomprimido; subirlo si algun recurso legitimo
    // necesita mas. Acota el alloc y la capacidad del decompress.
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
        *origOut = (void*)(site + 5 + *rel);
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
    for (const Pair& p : SITES)
    {
        RepointCall(p.newSite, (void*)hkNew, (void**)&oNew);
        RepointCall(p.decSite, (void*)hkDec, (void**)&oDec);
    }
}
