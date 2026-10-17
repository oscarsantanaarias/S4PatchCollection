#include <windows.h>

void InstallArcadeStackOverflowFix();
void InstallEnchantOOBFix();
void InstallHttpImageCacheFix();
void InstallStructVectorCapFix();
void InstallArcadeAllocFix();
void InstallLoadingTipCache();
void InstallX7DecryptGuard();
void InstallBlobTreeGuard();
void InstallFileExistsCache();
void InstallMemoryJumpFix();
void InstallSceneLeakFix();
void InstallS4hdMountGuard();
void InstallResWrapperLeakFix();
void InstallLzoS4SizeGuard();
void InstallP2PContainerDoSGuard();
void InstallIDocumentReloadLeakFix();
void InstallXmlOverReadGuard();
void InstallSmoothRenderFix();
void InstallDarkModeWindowFix();
void InstallCollisionResolverFix();
void StartGameEnhancements();
void StartOverlay();
void StartNewActorState();
void StartResourceLogTest();
void StartHdText();
extern "C" void StartHeapFixes(int lfh, int failsoft19, int badAlloc32);

static DWORD WINAPI InitThread(LPVOID)
{
    StartResourceLogTest();
    InstallDarkModeWindowFix();
    InstallSmoothRenderFix();
    InstallArcadeStackOverflowFix();
    InstallFileExistsCache();
    InstallMemoryJumpFix();
    InstallEnchantOOBFix();
    InstallHttpImageCacheFix();
    InstallStructVectorCapFix();
    InstallArcadeAllocFix();
    InstallLoadingTipCache();
    InstallX7DecryptGuard();
    InstallBlobTreeGuard();
    InstallSceneLeakFix();
    InstallS4hdMountGuard();
    InstallResWrapperLeakFix();
    InstallLzoS4SizeGuard();
    InstallP2PContainerDoSGuard();
    InstallIDocumentReloadLeakFix();
    InstallXmlOverReadGuard();
    StartHeapFixes(1, 0, 0);
    StartHdText();
    StartGameEnhancements();
    StartNewActorState();
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
