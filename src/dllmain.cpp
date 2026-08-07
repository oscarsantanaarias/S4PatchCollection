#include <windows.h>

void InstallFix_C03_ArcadeOverflow();
void InstallFix_C02_EnchantOOB();
void InstallFix_M01_HttpCacheCap();
void InstallFix_L01_StructVecCap();
void InstallFix_DoS_ArcadeAlloc();
void InstallFix_PerfG1_TipReload();
void InstallFix_S2_X7Decrypt();
extern "C" void StartHeapFixes(int lfh, int failsoft19, int badAlloc32);

static DWORD WINAPI InitThread(LPVOID)
{
    InstallFix_C03_ArcadeOverflow(); // patches C-03, arcade P2P entry reader stack overflow (LIVE, confirmed)
    // -- not-yet-live-tested: uncomment one at a time to activate + test --
    // InstallFix_C02_EnchantOOB(); // patches C-02, enchant-table file-index OOB write
    // InstallFix_M01_HttpCacheCap(); // patches M-01, unbounded HTTP image cache -> freeze
    // InstallFix_L01_StructVecCap(); // patches L-01, struct-vector count DoS
    // InstallFix_DoS_ArcadeAlloc(); // patches DoS, arcade stream alloc OOM
    // InstallFix_PerfG1_TipReload(); // perf G1, loading-tip table re-parse per screen
    // InstallFix_S2_X7Decrypt(); // patches S2, encrypted .x7 short-file underflow + OOM
    StartHeapFixes(1, 1, 0); // patches C-01, frame-map heap overflow + OOM fail-soft (badAlloc32 off, unproven)
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
