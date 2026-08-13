#pragma once
#include <windows.h>

// keep it cached, GetModuleHandleW hits the loader lock and S4 runs inside the hooks
__declspec(selectany) uintptr_t g_s4_base = 0;

static inline uintptr_t s4_base() {
    if (!g_s4_base) g_s4_base = (uintptr_t)GetModuleHandleW(NULL);
    return g_s4_base;
}

// Image base del cliente de esta rama: las addresses de abajo se rebasan contra la
// base real en runtime, asi que el mismo build sirve aunque el exe se relocalice.
#define S4(va) (s4_base() + ((va) - 0x00400000u))
