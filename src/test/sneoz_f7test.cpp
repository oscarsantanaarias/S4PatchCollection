#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#pragma comment(lib, "user32.lib")

// TEST-ONLY — read-only diagnostic. F7 reports game state + which manager list
// actually holds the players. Touches nothing.

namespace
{
    const uintptr_t GHIDRA_BASE = 0x00E80000;
    uintptr_t A_LIST   = 0x025BF550; // -> actor manager
    uintptr_t A_LOCAL  = 0x025BD770; // -> local player
    uintptr_t A_STATE  = 0x01BC4D70; // FUN_01bc4d70(x) -> game-state enum

    typedef int(__fastcall* tState)(int);

    char* EntryName(void* entry) // std::string at entry+0x2c, len at entry+0x40
    {
        unsigned len = *(unsigned*)((char*)entry + 0x40);
        return (len > 0xf) ? *(char**)((char*)entry + 0x2c) : (char*)entry + 0x2c;
    }

    int WalkList(void* listHead, char* nameOut, int nameCap) // returns node count
    {
        void* sentinel = *(void**)((char*)listHead + 4);
        if (!sentinel) return -1;
        int n = 0;
        nameOut[0] = 0;
        for (void* node = *(void**)sentinel; node && node != sentinel && n < 64; node = *(void**)node, ++n)
        {
            if (n == 0)
            {
                void* entry = *(void**)((char*)node + 8);
                if (entry) { char* nm = EntryName(entry); if (nm) lstrcpynA(nameOut, nm, nameCap); }
            }
        }
        return n;
    }

    void Diag(char* out, int cap)
    {
        __try
        {
            void* mgr = *(void**)A_LIST;
            void* actorList = mgr ? *(void**)((char*)mgr + 0xc) : nullptr;
            int actorCount = actorList ? *(int*)((char*)actorList + 8) : -1;

            char entryName[64] = "";
            int entryCount = mgr ? WalkList(mgr, entryName, sizeof(entryName)) : -1; // mgr+4 name list

            int state = -1;
            void* local = *(void**)A_LOCAL;
            if (local)
            {
                int sub = *(int*)((char*)local + 0x138);
                if (sub) state = ((tState)A_STATE)(sub);
            }

            wsprintfA(out,
                "game state = %d  (list populates when state==2)\n\n"
                "mgr = %p   local = %p\n\n"
                "ACTOR list (mgr+0xc) count = %d\n"
                "ENTRY list (mgr+4, by name) count = %d\n"
                "first entry name = \"%s\"\n\n"
                "-> if ENTRY count>0 the players live in mgr+4 (by name),\n"
                "   not in the mgr+0xc list the despawn/teleport resolves against.",
                state, mgr, local, actorCount, entryCount, entryName);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            lstrcpynA(out, "diagnostic faulted (bad pointer / not in game)", cap);
        }
    }

    DWORD WINAPI PollThread(LPVOID)
    {
        bool prev = false;
        for (;;)
        {
            Sleep(25);
            bool down = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            if (down && !prev)
            {
                char b[512];
                Diag(b, sizeof(b));
                MessageBoxA(NULL, b, "F7 DIAG", MB_OK | MB_TOPMOST);
            }
            prev = down;
        }
    }

    DWORD WINAPI InitThread(LPVOID)
    {
        intptr_t delta = (intptr_t)GetModuleHandleW(NULL) - (intptr_t)GHIDRA_BASE;
        A_LIST += delta; A_LOCAL += delta; A_STATE += delta;
        CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);
        MessageBoxA(NULL, "Diag loaded. In an active round press F7.", "F7 DIAG", MB_OK | MB_TOPMOST);
        return 0;
    }
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
