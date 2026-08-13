#include <windows.h>

// S1 - LZO1X sin tope de salida (FUN_00b615d0). El decompresor NO recibe capacidad
// de destino: solo incrementa el cursor de salida, nunca lo compara contra un fin
// de buffer. Varios callers lo usan contra buffers FIJOS -> overflow controlado por
// el stream (payload de otro peer en el dispatcher P2P, o listas S2C).
//
// El cliente YA tiene la variante SEGURA FUN_00b61bb0(src, srclen, dst, *cap): lee
// *cap como capacidad de entrada y acota la salida contra dst+cap, dejando en *cap
// la longitud producida (mismo out que el inseguro). Fix: repuntar cada call-site
// del inseguro a un shim que setea *cap = tamano real del buffer y llama al seguro.
// El inseguro queda intacto para los callers de alloc dinamico (no cubiertos aca).
namespace
{
    typedef int(__cdecl* tSafeDecompress)(unsigned short*, int, unsigned char*, int*);
    tSafeDecompress SafeDecompress = (tSafeDecompress)0x00B61BB0;

    int __cdecl shim2048(unsigned short* src, int srclen, unsigned char* dst, int* outLen)
    {
        *outLen = 0x800;
        return SafeDecompress(src, srclen, dst, outLen);
    }

    int __cdecl shim120000(unsigned short* src, int srclen, unsigned char* dst, int* outLen)
    {
        *outLen = 120000;
        return SafeDecompress(src, srclen, dst, outLen);
    }

    bool RepointCall(uintptr_t site, void* target)
    {
        unsigned char* p = (unsigned char*)site;
        if (*p != 0xE8)
            return false;
        DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(int*)(p + 1) = (int)((uintptr_t)target - (site + 5));
        VirtualProtect(p, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        return true;
    }
}

void InstallLzoUnboundedGuard()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    RepointCall(0x00AFE30F, (void*)shim2048);    // FUN_00afe210 dispatcher P2P -> global DAT_01182840 (0x800), red
    RepointCall(0x00A56428, (void*)shim120000);  // FUN_00a56380 lista S2C -> DAT_0111a0d8 (120000)
    RepointCall(0x00AD3155, (void*)shim120000);  // FUN_00ad30b0 lista S2C -> DAT_011375f8 (120000)
    RepointCall(0x00AD58A8, (void*)shim120000);  // FUN_00ad5800 lista S2C -> DAT_01154ab8 (120000)
}
