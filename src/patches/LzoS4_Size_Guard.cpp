#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <cstdint>
#include "../s4_base.h"

namespace
{
    // Cada caller LZO que hace new[](header+1) (con wrap 0xFFFFFFFF->0) y despues le
    // pasa el header crudo del atacante como capacidad a lzo1x_decompress_safe
    // (0x0103A0B0). Los dos sites de cada caller llevan el MISMO clamp sin estado:
    // el alloc y la capacidad sanitizan el header igual, asi que no se desincronizan.
    // new[] = 0x00FE5D3E (compartido), decompresor = 0x0103A0B0.
    //
    // La lista sale de escanear el binario entero por E8 -> 0x0103A0B0: 10 sitios, de
    // los cuales 9 tienen su new[] pegado (los vulnerables). El decimo (0x00DF27F9) es
    // un wrapper fino que no allocatea, por eso no entra.
    struct Pair { uintptr_t newSite; uintptr_t decSite; };
    const Pair SITES[] = {
        { 0x004E7587, 0x004E76FA }, // FUN_004E6970  (decoder encriptado generico, ^0xFE292513)
        { 0x00B70B1E, 0x00B70B41 }, // FUN_00B70AB0  (header crudo leido con read(&local,4))
        { 0x0100413B, 0x010041FB }, // FUN_01003D70  (contenedor .x7 encriptado)
        { 0x010A52BF, 0x010A52F7 }, // FUN_010A5010  (.s4)
        { 0x010C6EAC, 0x010C6EE4 }, // FUN_010C6BF0  (.s4)
        { 0x010C77EC, 0x010C7824 }, // FUN_010C7530  (.s4, gemela estructural)
        { 0x010C813C, 0x010C8174 }, // FUN_010C7E80  (.s4, gemela estructural)
        { 0x010C8A96, 0x010C8ACE }, // FUN_010C87E0  (.s4, gemela estructural)
        { 0x010F7D1F, 0x010F7D57 }, // FUN_010F7A50  (.s4)
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
        RepointCall(S4(p.newSite), (void*)hkNew, (void**)&oNew);
        RepointCall(S4(p.decSite), (void*)hkDec, (void**)&oDec);
    }
}
