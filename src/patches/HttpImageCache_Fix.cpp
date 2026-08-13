#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    // El cache de texturas HTTP es un std::map por url con lower_bound + insert-if-
    // absent, sin eviccion ni tope: cada textura que baja entra y ninguna se saca.
    // Se corta en CACHE_CAP devolviendo un iterador nulo desde el insert del map.
    //
    // Aca se hookea la funcion del insert y no un call site: sus 3 xrefs estan todas
    // dentro de la misma operacion del map, asi que el hook cubre los tres caminos y
    // no toca ningun otro contenedor.
    const uintptr_t INSERT_FN = 0x004B5C60;
    const long      CACHE_CAP = 1024;

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
}

void InstallHttpImageCacheFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    oInsert = (tInsert)INSERT_FN;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oInsert, guardInsert);
    DetourTransactionCommit();
}
