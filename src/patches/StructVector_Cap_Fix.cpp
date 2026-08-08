#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace
{
    const uintptr_t CMP_INSN     = 0x019185DA;
    const uintptr_t CMP_DISP32   = 0x019185DC;
    volatile long   g_l01_cap    = 0x40000;
    volatile long   g_l01_blocked = 0;
}

void InstallStructVectorCapFix()
{
    if (*(uint8_t*)CMP_INSN != 0x3B || *(uint8_t*)(CMP_INSN + 1) != 0x05)
        return;
    DWORD old;
    if (!VirtualProtect((void*)CMP_DISP32, 4, PAGE_EXECUTE_READWRITE, &old))
        return;
    *(uint32_t*)CMP_DISP32 = (uint32_t)(uintptr_t)&g_l01_cap;
    VirtualProtect((void*)CMP_DISP32, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)CMP_INSN, 8);
    (void)g_l01_blocked;
}
