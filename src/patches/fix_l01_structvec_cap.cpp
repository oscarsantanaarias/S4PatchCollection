#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace
{
    // CMsg_ReadStructVector_capped @ 0x01918580 validates the element count with
    //   cmp eax, ds:dword_256D3BC   (the shared 0x100000 cap, at 0x019185DA)
    // then prealloc + loops that many times, ignoring per-element read failure.
    // We can't lower the shared global (other readers need the 1MB cap), so we
    // repoint ONLY this instruction's disp32 operand to our own, smaller cap.
    const uintptr_t CMP_INSN     = 0x019185DA; // cmp eax, ds:dword_256D3BC  -> 3B 05 <disp32>
    const uintptr_t CMP_DISP32   = 0x019185DC; // the disp32 operand
    volatile long   g_l01_cap    = 0x40000;    // ponytail: 256K element ceiling (prealloc <=16MB, <=256K iters); far above any real struct-vector, upgrade path = tune per element type

    volatile long   g_l01_blocked = 0; // bumped indirectly when the reader throws on an over-cap count
}

void InstallFix_L01_StructVecCap()
{
    if (*(uint8_t*)CMP_INSN != 0x3B || *(uint8_t*)(CMP_INSN + 1) != 0x05)
        return; // not the expected `cmp eax, [disp32]` - bail rather than corrupt code
    DWORD old;
    if (!VirtualProtect((void*)CMP_DISP32, 4, PAGE_EXECUTE_READWRITE, &old))
        return;
    *(uint32_t*)CMP_DISP32 = (uint32_t)(uintptr_t)&g_l01_cap;
    VirtualProtect((void*)CMP_DISP32, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)CMP_INSN, 8);
    (void)g_l01_blocked;
}
