#include <windows.h>

void InstallFix_C03_ArcadeOverflow();

static DWORD WINAPI InitThread(LPVOID)
{
    InstallFix_C03_ArcadeOverflow(); // patches C-03, arcade P2P entry reader stack overflow
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
