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
void StartGameEnhancements();
void StartNewActorState();
extern "C" void StartHeapFixes(int lfh, int failsoft19, int badAlloc32);

static DWORD WINAPI InitThread(LPVOID)
{
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
    // SIDE EFFECT conocido: el anti-reload por-slot (FIX 2, ventana 500ms de los
    // 10 slots de SetScene) rara vez saltea recargar un slot -> un item del
    // collectionbook queda con el scene stale (bloque rojo/roto). Si molesta:
    // ANTIRELOAD=false (o bajar ANTIRELOAD_MS) en Scene_Leak_Fix.cpp.
    InstallSceneLeakFix();
    InstallS4hdMountGuard();
    InstallResWrapperLeakFix();
    InstallLzoS4SizeGuard();
    InstallP2PContainerDoSGuard();
    InstallIDocumentReloadLeakFix();
    InstallXmlOverReadGuard();
    StartHeapFixes(1, 1, 0);
    StartGameEnhancements();
    StartNewActorState();
    LoadLibraryA("inflar.dll");
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
