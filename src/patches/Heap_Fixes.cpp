#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include "../s4_base.h"
#pragma comment(lib, "detours.lib")

static const DWORD CPP_EXC = 0xE06D7363;
static volatile LONG g_caught = 0;

// Low Fragmentation Heap en todos los heaps del proceso, incluidos los que aparecen
// despues. Sin esto la fragmentacion termina en un cuelgue con la RAM a la mitad.
static HANDLE g_known[128]; static int g_seen = 0;
static bool AlreadyKnown(HANDLE h) {
    for (int i = 0; i < g_seen; i++) if (g_known[i] == h) return true;
    if (g_seen < 128) g_known[g_seen++] = h;
    return false;
}
static void SweepHeapsLFH() {
    DWORD n = GetProcessHeaps(0, NULL); if (!n) return;
    HANDLE* heaps = (HANDLE*)HeapAlloc(GetProcessHeap(), 0, n * sizeof(HANDLE));
    if (!heaps) return;
    DWORD got = GetProcessHeaps(n, heaps);
    for (DWORD i = 0; i < got; i++) {
        AlreadyKnown(heaps[i]);
        ULONG info = 0; SIZE_T ret = 0;
        ULONG before = HeapQueryInformation(heaps[i], HeapCompatibilityInformation, &info, sizeof(info), &ret) ? info : (ULONG)-1;
        if (before != 2) { ULONG lfh = 2; HeapSetInformation(heaps[i], HeapCompatibilityInformation, &lfh, sizeof(lfh)); }
    }
    HeapFree(GetProcessHeap(), 0, heaps);
}
static DWORD WINAPI LfhThread(LPVOID) {
    SweepHeapsLFH();
    for (;;) { Sleep(5000); SweepHeapsLFH(); }
}

// Fail-soft de allocators: el throw de bad_alloc no lo atrapa nadie y mata el proceso.
// Devolver NULL deja al caller manejar el fallo. ADDR 0 = no localizada, no se hookea.
typedef void* (__fastcall* tA)(void*, void*, int);
#define HOOK_A(NAME, ADDR)                                                            \
  static const uintptr_t va_##NAME = (ADDR);                                          \
  static tA o_##NAME = nullptr;                                                       \
  static void* __fastcall h_##NAME(void* ecx, void* edx, int a1) {                    \
    __try { return o_##NAME(ecx, edx, a1); }                                          \
    __except (GetExceptionCode()==CPP_EXC ? EXCEPTION_EXECUTE_HANDLER                 \
                                          : EXCEPTION_CONTINUE_SEARCH) {              \
      InterlockedIncrement(&g_caught);                                                \
      return 0;                                                                       \
    }                                                                                 \
  }
HOOK_A(a00fa5220, 0x00FA5220)
HOOK_A(a00de8040, 0x00DE8040)
HOOK_A(a00f48780, 0x00F48780)
HOOK_A(a00f48ae0, 0x00F48AE0)
HOOK_A(a00fa0580, 0x00FA0580)
HOOK_A(a00fa06c0, 0x00FA06C0)
HOOK_A(a00fa0850, 0x00FA0850)
HOOK_A(a00fb5960, 0x00FB5960)
HOOK_A(a00f834a0, 0x00F834A0)
HOOK_A(a00fb5a30, 0x00FB5A30)
HOOK_A(a00fc0590, 0x00FC0590)
HOOK_A(a00fc06a0, 0x00FC06A0)
HOOK_A(a0112a210, 0x0112A210)

typedef void* (__fastcall* tB)(void*, void*, void*, unsigned);
static const uintptr_t va_b00fa56e0 = 0x00FA56E0;
static tB o_b00fa56e0 = nullptr;
static void* __fastcall h_b00fa56e0(void* ecx, void* edx, void* a1, unsigned a2) {
    __try { return o_b00fa56e0(ecx, edx, a1, a2); }
    __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        InterlockedIncrement(&g_caught);
        return 0;
    }
}

// Este ademas rechaza el count fuera de rango del frame-map antes de que allocatee.
static const uintptr_t va_c00fce320 = 0x00FCE320;
static tA o_c00fce320 = nullptr;
static void* __fastcall h_c00fce320(void* ecx, void* edx, int a1) {
    if ((unsigned)a1 > 0x100000u) {
        InterlockedIncrement(&g_caught);
        return 0;
    }
    __try { return o_c00fce320(ecx, edx, a1); }
    __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        InterlockedIncrement(&g_caught);
        return 0;
    }
}

#define ATTACH_A(NAME) do { if (va_##NAME) { o_##NAME = (tA)S4(va_##NAME);            \
    DetourAttach(&(PVOID&)o_##NAME, h_##NAME); } } while (0)

extern "C" void StartHeapFixes(int lfh, int failsoft, int unused) {
    (void)unused;
    // no reinstalar: los punteros o_ ya apuntan a los trampolines de Detours,
    // enganchar de nuevo hookearia el trampolin y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    if (lfh) CreateThread(0, 0, LfhThread, 0, 0, 0);
    if (!failsoft) return;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    ATTACH_A(a00fa5220);
    ATTACH_A(a00de8040);
    ATTACH_A(a00f48780);
    ATTACH_A(a00f48ae0);
    ATTACH_A(a00fa0580);
    ATTACH_A(a00fa06c0);
    ATTACH_A(a00fa0850);
    ATTACH_A(a00fb5960);
    ATTACH_A(a00f834a0);
    ATTACH_A(a00fb5a30);
    ATTACH_A(a00fc0590);
    ATTACH_A(a00fc06a0);
    ATTACH_A(a0112a210);
    ATTACH_A(c00fce320);

    o_b00fa56e0 = (tB)S4(va_b00fa56e0);
    DetourAttach(&(PVOID&)o_b00fa56e0, h_b00fa56e0);

    DetourTransactionCommit();
}

extern "C" void StopHeapFixes() { }
extern "C" __declspec(dllexport) long OomCaught() { return g_caught; }
