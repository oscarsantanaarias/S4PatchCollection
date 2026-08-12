#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"

// BattlEye is gone, but the client still runs its init and pops up
// "Battleye Start Error" when it can't reach it.
//
//   TEST ECX, ECX
//   JNZ  short over_the_battleye_block     <- 75 5E
//
// Falling through is what runs the init. Turning the conditional jump into an
// unconditional one skips the whole block, one byte, nothing relocated.
static const uintptr_t BE_JNZ = 0x00F16E6F;

// This one fires during startup, long before d3d9 is around, so it can't sit
// behind the usual wait. On a packed client the bytes are still encrypted for
// the first instant, so spin until the signature shows up and patch the moment
// it does. Bail after 10s rather than spin forever on a client that never matches.
void InstallBattlEyeBypass()
{
    BYTE* p = (BYTE*)S4(BE_JNZ);

    for (int i = 0; i < 10000; ++i)
    {
        if (!IsBadReadPtr(p, 2) && p[0] == 0x75 && p[1] == 0x5E)
        {
            DWORD old;
            if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old))
                return;
            p[0] = 0xEB;
            VirtualProtect(p, 1, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 1);
            return;
        }
        Sleep(1);
    }
}
