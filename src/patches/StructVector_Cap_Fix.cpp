#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"
#include <cstdint>

namespace
{
    const uintptr_t CMP_INSN     = 0x00E007AA;
    const uintptr_t CMP_DISP32   = 0x00E007AC;
    volatile long   g_l01_cap    = 0x40000;
    volatile long   g_l01_blocked = 0;
}

void InstallStructVectorCapFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    const uintptr_t insn = S4(CMP_INSN);
    const uintptr_t disp = S4(CMP_DISP32);
    if (*(uint8_t*)insn != 0x3B || *(uint8_t*)(insn + 1) != 0x05)
        return;
    DWORD old;
    if (!VirtualProtect((void*)disp, 4, PAGE_EXECUTE_READWRITE, &old))
        return;
    *(uint32_t*)disp = (uint32_t)(uintptr_t)&g_l01_cap;
    VirtualProtect((void*)disp, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)insn, 8);
    (void)g_l01_blocked;
}
