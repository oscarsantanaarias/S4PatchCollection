#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void InstallMemoryJumpFix()
{
    BYTE* site = (BYTE*)0x01AEC298;
    DWORD old;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old))
        return;
    if (*site == 0x79)
        *site = 0xEB;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
}
