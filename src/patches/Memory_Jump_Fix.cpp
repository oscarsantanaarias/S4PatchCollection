#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"

// El TEST EDX,EDX / JNS saltea el fill de la struct de error y deja campos sin
// inicializar. Se fuerza el salto a incondicional (0x79 -> 0xEB), un byte.
void InstallMemoryJumpFix()
{
    BYTE* site = (BYTE*)S4(0x00FAC9B8);
    DWORD old;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old))
        return;
    if (*site == 0x79)
        *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
}
