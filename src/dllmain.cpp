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
void InstallBattlEyeBypass();
void StartGameEnhancements();
void StartNewActorState();
extern "C" void StartHeapFixes(int lfh, int failsoft19, int badAlloc32);

static DWORD WINAPI InitThread(LPVOID)
{
    // this one has to land before the client boots, it can't wait for d3d9
    InstallBattlEyeBypass();

    // wait for d3d9, patching too early gets us overwritten
    for (int i = 0; i < 600 && !GetModuleHandleA("d3d9.dll"); ++i)
        Sleep(50);
    Sleep(500);

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
    // known side effect: the per-slot anti-reload (500ms window over the 10 SetScene
    // slots) sometimes skips a reload and leaves a collectionbook item on a stale
    // scene, which shows up as a red/broken block. ANTIRELOAD=false or a lower
    // ANTIRELOAD_MS in Scene_Leak_Fix.cpp if it gets annoying.
    InstallSceneLeakFix();
    InstallS4hdMountGuard();
    InstallResWrapperLeakFix();
    InstallLzoS4SizeGuard();
    InstallP2PContainerDoSGuard();
    InstallIDocumentReloadLeakFix();
    InstallXmlOverReadGuard();
   // y el heapfixes me dio crash
    StartHeapFixes(1, 1, 0);
    StartGameEnhancements();
    //actor crash
    StartNewActorState();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        LoadLibraryA("mutex.dll");
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
