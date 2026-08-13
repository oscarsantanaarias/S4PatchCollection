#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// El salto se come el fill de la struct de error y la deja con +0x4, +0x8, +0xc y +0x10
// sin inicializar, o sea el caller lee basura de pila. Se fuerza el salto a incondicional,
// un byte, para que el fill corra siempre.
//
// Aca el codegen usa CMP contra un registro en cero + JGE (0x7D) en vez del TEST + JNS de
// otros builds, pero el tramo es el mismo: escribe [ESI], compara, y el salto saltea el
// bloque que llena la struct.
void InstallMemoryJumpFix()
{
    BYTE* site = (BYTE*)0x00B47264;
    DWORD old;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old))
        return;
    if (*site == 0x7D)
        *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
}
