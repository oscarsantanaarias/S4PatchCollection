#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include "../s4_base.h"

// ============================================================================
// S4 League Season 8 - Smooth Render & Zero-Delay Extrapolator Patch
// Eliminates artificial 70ms interpolation lag on actor positions and movements.
// ============================================================================

typedef int (__stdcall* FnExtrapolatorCore)(int currentTime, int a2, int a3, int a4);
typedef int (__stdcall* FnGetTransformActor)(int currentTime, int a2, int a3, int a4);

static FnGetTransformActor OriginalGetTransformActor = nullptr;

static int __stdcall Hook_GetTransformActor(int currentTime, int a2, int a3, int a4)
{
    const int delay = 0;
    auto pCore = reinterpret_cast<FnExtrapolatorCore>(S4(0x00B7E290));
    return pCore(currentTime - delay, a2, a3, a4);
}

void InstallSmoothRenderFix()
{
    static bool installed = false;
    if (installed) return;
    installed = true;

    // 1. Modificar variables globales directamente a 0
    uintptr_t pActorDelay = S4(0x015F498C);
    DWORD oldProtect = 0;
    if (VirtualProtect(reinterpret_cast<void*>(pActorDelay), 8, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        *reinterpret_cast<int*>(pActorDelay) = 0;       // Actor delay (def: 70ms)
        *reinterpret_cast<int*>(pActorDelay + 4) = 0;   // Ghost delay (def: 100ms)
        VirtualProtect(reinterpret_cast<void*>(pActorDelay), 8, oldProtect, &oldProtect);
    }

    // 2. Instalar hook a nivel de funcion por Detours
    OriginalGetTransformActor = reinterpret_cast<FnGetTransformActor>(S4(0x00B7E1F0));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&OriginalGetTransformActor), Hook_GetTransformActor);
    DetourTransactionCommit();
}

