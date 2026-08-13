#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Low Fragmentation Heap en todos los heaps del proceso, incluidos los que aparecen
// despues. Sin esto la fragmentacion termina en un cuelgue con la RAM a la mitad.
//
// El resto de Heap_Fixes (los wrappers de allocator con fail-soft del bad_alloc) no va
// en este build: la familia CFastArray existe pero el count*8 esta inlineado y no se
// aisla, asi que no hay funcion que hookear. Queda solo el LFH, que es API de Windows
// y no depende de ninguna address del cliente.
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

extern "C" void StartHeapFixes(int lfh, int failsoft, int unused) {
    (void)failsoft; (void)unused;
    // no reinstalar: una segunda llamada dejaria otro hilo barriendo los heaps
    static bool installed = false;
    if (installed) return;
    installed = true;

    if (lfh) CreateThread(0, 0, LfhThread, 0, 0, 0);
}

extern "C" void StopHeapFixes() { }
