#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

// Consola de diagnostico: una linea que se reescribe con los contadores en vivo.
// Poner ENABLED en false para no crearla.
static const bool ENABLED = true;

extern "C" long FileExistsServed();
extern "C" long FileExistsDisk();

static DWORD WINAPI ConsoleThread(LPVOID)
{
    for (;;)
    {
        const long served = FileExistsServed();
        const long disk   = FileExistsDisk();
        const long total  = served + disk;
        const double ahorro = total ? (100.0 * served / total) : 0.0;

        printf("\rlookups %-9ld disco %-8ld cache %-9ld ahorro %5.1f%%",
               total, disk, served, ahorro);
        fflush(stdout);
        Sleep(500);
    }
}

void StartConsole()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    if (!ENABLED) return;
    if (!AllocConsole()) return;

    SetConsoleTitleA("s4fixes");
    FILE* out = nullptr;
    freopen_s(&out, "CONOUT$", "w", stdout);
    if (!out) return;

    printf("s4fixes\n\n");
    CreateThread(nullptr, 0, ConsoleThread, nullptr, 0, nullptr);
}
