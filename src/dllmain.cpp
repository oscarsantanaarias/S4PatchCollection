#include <windows.h>

void InstallFileExistsCache();
void InstallArcadeAllocFix();
void InstallX7DecryptGuard();
void InstallLzoS4SizeGuard();
void InstallXmlOverReadGuard();
void InstallHttpImageCacheFix();
void InstallResWrapperLeakFix();
void InstallManifestReadClamp();
void InstallLzoUnboundedGuard();
void InstallMemoryJumpFix();
void InstallChaserAnnounceReplayFix();
void StartGameEnhancements();
void StartOverlay();
extern "C" void StartHeapFixes(int lfh, int failsoft, int unused);

static DWORD WINAPI InitThread(LPVOID)
{
    InstallFileExistsCache();
    InstallArcadeAllocFix();
    InstallX7DecryptGuard();
    InstallLzoS4SizeGuard();
    InstallXmlOverReadGuard();
    InstallHttpImageCacheFix();
    InstallResWrapperLeakFix();
    InstallManifestReadClamp();
    InstallLzoUnboundedGuard();
    InstallMemoryJumpFix();
    InstallChaserAnnounceReplayFix();
    StartHeapFixes(1, 0, 0);
    StartGameEnhancements();
    StartOverlay();
    
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
