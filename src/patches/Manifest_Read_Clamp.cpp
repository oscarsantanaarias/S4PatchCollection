#include <windows.h>
#include "detours.h"

// S1 - manifest/list parser stack overflow (FUN_00b641f0 y FUN_00b63ac0).
// Ambos leen un largo de 4 bytes del stream y copian esa cantidad a un buffer de
// pila de 276 con el reader crudo FUN_00b62740 (copia len bytes sin conocer la
// capacidad del destino). Los DOS unicos callers del reader usan buffers de 276.
// Fix: clamp del largo a <276 en el reader (choke point) -> ninguna copia pasa del
// buffer. Los reads chicos (version/count = 4 bytes) no se tocan.
namespace
{
    const uintptr_t MAN_READ    = 0x00B62740; // void __thiscall(this, dst, len)
    const unsigned  MAN_BUFFER  = 276;

    typedef void(__fastcall* tManRead)(void*, void*, void*, unsigned);
    tManRead oManRead = (tManRead)MAN_READ;

    void __fastcall hkManRead(void* thisptr, void* edx, void* dst, unsigned len)
    {
        if (len > MAN_BUFFER)
            len = MAN_BUFFER - 1;
        oManRead(thisptr, edx, dst, len);
    }
}

void InstallManifestReadClamp()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oManRead, hkManRead);
    DetourTransactionCommit();
}
