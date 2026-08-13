#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <cstring>
#include "../s4_base.h"

#pragma comment(lib, "detours.lib")

typedef int(__fastcall* get_current_state_id_t)(int);
static get_current_state_id_t orig_get_current_state_id = (get_current_state_id_t)S4(0x00F8A130);

typedef void* (__thiscall* get_state_t)(void*, int);
static get_state_t orig_get_state = (get_state_t)S4(0x00F8A1C0);

typedef void(__thiscall* set_current_state_t)(void*, void*);
static set_current_state_t orig_set_current_state = (set_current_state_t)S4(0x00F8A0B0);

static const size_t k_clone_size = 0x100;
static void* g_current_manager = nullptr;
static int g_active_custom_id = -1;
static DWORD g_active_custom_until = 0;
static const DWORD k_custom_hold_ms = 300;

// ==== Estados custom: agrega/edita lineas (id, source, tecla VK 0=ninguna) ====
struct CustomState { int id; int source; int key; void* clone; };
static CustomState g_custom[] = {
    //  id      source                    key(VK)   clone
    { 0x45,    0x05,  /*NORMAL*/         0x61,     nullptr },   // Numpad1
    { 0x46,    0x2D,  /*WEAPON1_JUMP*/   0x62,     nullptr },   // Numpad2
    { 0x47,    0x1F,  /*SKILL_FLY*/      0x63,     nullptr },   // Numpad3
    { 0x48,    0x08,  /*JUMP*/           0x64,     nullptr },   // Numpad4
    { 0x49,    0x15,  /*FASTRUN*/        0x65,     nullptr },   // Numpad5
    { 0x4A,    0x06,  /*RUN*/            0x66,     nullptr },   // Numpad6
    { 0x4B,    0x22,  /*SKILL_SHIELD*/   0x67,     nullptr },   // Numpad7
    { 0x4C,    0x2C,  /*WEAPON1_STRONG*/ 0x68,     nullptr },   // Numpad8
    { 0x4D,    0x26,  /*SKILL_BERSERK*/  0x69,     nullptr },   // Numpad9
    { 0x4E,    0x1D,  /*IDLE*/           0x60,     nullptr },   // Numpad0
};
static const int g_customCount = sizeof(g_custom) / sizeof(g_custom[0]);

static void* GetRealState(void* manager, int id)
{
    void* p = nullptr;
    __try { p = orig_get_state(manager, id); }
    __except (EXCEPTION_EXECUTE_HANDLER) { p = nullptr; }
    return p;
}

static CustomState* FindCustom(int id)
{
    for (int i = 0; i < g_customCount; i++)
        if (g_custom[i].id == id)
            return &g_custom[i];
    return nullptr;
}

static void* GetOrCreateClone(void* manager, CustomState* cs)
{
    if (cs->clone)
        return cs->clone;

    void* src = GetRealState(manager, cs->source);
    if (!src)
        return nullptr;

    void* clone = VirtualAlloc(nullptr, k_clone_size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!clone)
        return nullptr;

    __try {
        memcpy(clone, src, k_clone_size);
        *(uint32_t*)((uint8_t*)clone + 0x20) = cs->id;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        VirtualFree(clone, 0, MEM_RELEASE);
        return nullptr;
    }

    cs->clone = clone;
    return clone;
}

static void ApplyCustomStateById(int id)
{
    if (!g_current_manager)
        return;

    CustomState* cs = FindCustom(id);
    if (!cs)
        return;

    __try {
        void* clone = GetOrCreateClone(g_current_manager, cs);
        if (clone)
            orig_set_current_state(g_current_manager, clone);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    g_active_custom_id = id;
    g_active_custom_until = GetTickCount() + k_custom_hold_ms;
}

static void* __fastcall patched_get_state(void* this_ptr, void* edx, int state_id)
{
    (void)edx;

    CustomState* cs = FindCustom(state_id);
    if (cs) {
        void* c = GetOrCreateClone(this_ptr, cs);
        if (c)
            return c;
    }

    return orig_get_state(this_ptr, state_id);
}

static int __fastcall patched_get_current_state_id(int param_1)
{
    int result = orig_get_current_state_id(param_1);
    g_current_manager = (void*)param_1;

    if (g_active_custom_id >= 0 && GetTickCount() < g_active_custom_until)
        result = g_active_custom_id;

    return result;
}

static DWORD WINAPI PollThread(LPVOID)
{
    while (true) {
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);

        if (pid == GetCurrentProcessId()) {
            for (int i = 0; i < g_customCount; i++) {
                int k = g_custom[i].key;
                if (k && (GetAsyncKeyState(k) & 1))
                    ApplyCustomStateById(g_custom[i].id);
            }
        }

        Sleep(15);
    }
    return 0;
}

void StartNewActorState()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_get_current_state_id, (PVOID)patched_get_current_state_id);
    DetourAttach(&(PVOID&)orig_get_state, (PVOID)patched_get_state);
    DetourTransactionCommit();

    CreateThread(nullptr, 0, PollThread, nullptr, 0, nullptr);
}
