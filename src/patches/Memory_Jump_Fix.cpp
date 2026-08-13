#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Rama de valor negativo: cuando el valor entra en negativo, el bloque lo guarda tal cual
// en la struct y retorna ok, y el caller despues lo usa como tamano. Forzar el salto a
// incondicional hace que esa rama no corra nunca.
//
// Aca el codegen usa CMP contra un registro en cero + JGE (0x7D) donde otros builds usan
// TEST + JNS (0x79), pero el tramo es el mismo.
//
// Idempotente: solo escribe si el byte sigue siendo el condicional, asi que aplicarlo dos
// veces (o sobre un exe ya parcheado a mano) no hace nada.
void InstallMemoryJumpFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    BYTE* site = (BYTE*)0x00B47264;
    DWORD old;
    if (*site == 0xEB)
        return;                       // ya aplicado
    if (*site != 0x7D)
        return;                       // no es el sitio esperado, no se toca
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old))
        return;
    *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
}
