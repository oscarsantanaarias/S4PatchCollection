#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace
{
    // LoadRandomLoadingTip_x7 (0x013DA240) clears + re-parses the whole
    // Language/_rc_ingame_loading_tip_text.x7 table on EVERY loading screen,
    // then just picks a random tip. LoadStringTable_ClearAndReparse (0x01B5A210)
    // is shared, so we repoint ONLY its call site inside LoadRandomLoadingTip
    // (0x013DA462) to a guard that parses once; the random pick still runs each
    // time over the already-loaded table. Worst case if the table is cleared
    // elsewhere: missing tips (cosmetic), never a crash.
    const uintptr_t REPARSE_FN = 0x01B5A210;
    const uintptr_t CALL_SITE  = 0x013DA462; // call LoadStringTable_ClearAndReparse inside LoadRandomLoadingTip

    typedef void(__fastcall* tReparse)(void* thisTbl, void* edx, void* Str);
    tReparse oReparse = (tReparse)REPARSE_FN;
    volatile long g_tipLoaded = 0;

    void __fastcall guardReparse(void* thisTbl, void* edx, void* Str)
    {
        if (InterlockedCompareExchange(&g_tipLoaded, 1, 0) == 0)
            oReparse(thisTbl, edx, Str); // first loading screen: real parse. after: reuse.
    }

    bool RepointCall(uintptr_t site, void* target)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        DWORD old;
        if (!VirtualProtect((void*)(site + 1), 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect((void*)(site + 1), 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallFix_PerfG1_TipReload()
{
    RepointCall(CALL_SITE, (void*)guardReparse);
}
