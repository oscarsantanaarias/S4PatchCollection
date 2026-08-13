#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <detours.h>
#include <intrin.h>
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "../s4_base.h"

#pragma comment(lib, "detours.lib")
#pragma intrinsic(_ReturnAddress)

typedef LONG(NTAPI* NtDelayExecution_t)(BOOLEAN, PLARGE_INTEGER);
typedef LONG(NTAPI* NtQueryTimerResolution_t)(PULONG, PULONG, PULONG);
typedef LONG(NTAPI* NtSetTimerResolution_t)(ULONG, BOOLEAN, PULONG);
static NtDelayExecution_t       pNtDelayExecution = nullptr;
static NtQueryTimerResolution_t pNtQueryTimerResolution = nullptr;
static NtSetTimerResolution_t   pNtSetTimerResolution = nullptr;

// not static: the overlay drives these live, see overlay/Overlay.cpp
int max_framerate = 144;
int field_of_view = 90;
int center_field_of_view = 100;
int sprint_field_of_view = 110;

float normal_jump_height_multiplier = 0.953f;
float flight_multiplier = 1.0f;
float flight_evade_up_multiplier = 1.8f;
float exo_jump_multiplier = 0.653f;

static float frametime = 16.666666f;
static float speed_dampeners[9];
static float set_drop_val = 0.0f;

static const char* configPath = "config.ini";

static float ReadConfigFloat(const char* section, const char* key, float fallback)
{
    char value[64]{};
    GetPrivateProfileStringA(section, key, "", value, sizeof(value), configPath);
    if (value[0] == '\0')
        return fallback;
    return strtof(value, nullptr);
}

void patch_min_frametime(double min_frametime);

// the overlay calls this after moving the fps slider. FOV is read every frame so
// it needs nothing, but the frame limiter is patched into memory once.
void ApplyFrameRate()
{
    if (max_framerate > 0)
        patch_min_frametime(1.0 / max_framerate);
}

static void LoadConfig()
{
    max_framerate = GetPrivateProfileIntA("fps", "max_framerate", max_framerate, configPath);
    field_of_view = GetPrivateProfileIntA("fov", "field_of_view", field_of_view, configPath);
    center_field_of_view = GetPrivateProfileIntA("fov", "center_field_of_view", center_field_of_view, configPath);
    sprint_field_of_view = GetPrivateProfileIntA("fov", "sprint_field_of_view", sprint_field_of_view, configPath);
    exo_jump_multiplier = ReadConfigFloat("physics", "exo_jump_multiplier", exo_jump_multiplier);
    flight_evade_up_multiplier = ReadConfigFloat("physics", "flight_evade_up_multiplier", flight_evade_up_multiplier);
}

struct game_context {
    uint8_t unknown[0x48];
    uint8_t online_verbose_toggle;
    uint8_t fps_limiter_toggle;
};

typedef game_context* (__cdecl* fetch_game_context_t)(void);
static fetch_game_context_t fetch_game_context = (fetch_game_context_t)S4(0x004ad790);

typedef void(__thiscall* game_tick_t)(void*);
typedef void(__thiscall* move_actor_by_t)(void*, float, float, float);
typedef void(__thiscall* fun_005e4020_t)(void*, uint32_t);
typedef void(__thiscall* fov_consumer_t)(void*, uint32_t, uint32_t);
typedef void(__thiscall* calc_spread_t)(void*, uint32_t, uint8_t);

static game_tick_t     orig_game_tick = nullptr;
static move_actor_by_t orig_move_actor_by = nullptr;
static fun_005e4020_t  orig_fun_005e4020 = nullptr;
static fov_consumer_t  orig_fov_update = nullptr;
static calc_spread_t   orig_calculate_weapon_spread = nullptr;

struct funny_value { uint32_t value_xor; uint32_t value_xor_flip; uint32_t xor_key; };
static uint32_t get_funny_value(funny_value* x) { return x->value_xor ^ x->xor_key; }
static void set_funny_value(funny_value* x, uint32_t* value) {
    x->value_xor = *value ^ x->xor_key;
    x->value_xor_flip = ~x->xor_key;
}

void patch_min_frametime(double min_frametime);

struct actor_ctx {
    uint8_t unknown[0xb0];
    uint8_t actor_state;
    uint8_t unknown_2[0x3];
    uint32_t actor_substate_1;
    uint32_t actor_substate_2;
};

struct ctx_fun_005e4020 {
    uint8_t unknown[0x2d0];
    float set_drop_val;
};

static actor_ctx* fetch_actor_ctx() {
    typedef actor_ctx* (__cdecl* fetch_ctx_t)(void);
    static fetch_ctx_t fetch_ctx = (fetch_ctx_t)S4(0x004ae0a0);
    return fetch_ctx();
}

static float smoothedFPS = 0.0f;

static void UpdateJumpMultiplierByFPS()
{
    if (frametime > 0) {
        float fps = 1000.0f / frametime;
        smoothedFPS = (smoothedFPS * 0.9f) + (fps * 0.1f);
        if (smoothedFPS < 90.0f)
            normal_jump_height_multiplier = 1.0f;
        else
            normal_jump_height_multiplier = 0.953f;
    }
}

static void __fastcall patched_fun_005e4020(void* ecx, void* edx, uint32_t param_1)
{
    ctx_fun_005e4020* ctx = (ctx_fun_005e4020*)ecx;
    orig_fun_005e4020(ctx, param_1);

    float drop_val = ctx->set_drop_val;
    float drop_diff = drop_val + 50000.0f;
    if (drop_diff < 0.0f) drop_diff = -drop_diff;

    // -50000 es el valor de drop que identifica la PS.
    if (drop_diff < 1.0f)
        set_drop_val = drop_val;
}

// this+0x158 = target_fov (60 normal / 66 center / 80 sprint). Se restaura el valor del
// juego al salir: si dejamos el nuestro, la maquina de estados no vuelve a escribir
// 60/66/80 y el sprint se queda sin su valor.
static void __fastcall patched_fov_update(void* ecx, void*, uint32_t a1, uint32_t a2)
{
    float* target_fov = (float*)((char*)ecx + 0x158);
    const float orig = *target_fov;

    if (*target_fov == 60.0f)       *target_fov = static_cast<float>(field_of_view);
    else if (*target_fov == 66.0f)  *target_fov = static_cast<float>(center_field_of_view);
    else if (*target_fov == 80.0f)  *target_fov = static_cast<float>(sprint_field_of_view);

    orig_fov_update(ecx, a1, a2);

    *target_fov = orig;
}

// Weapon spread frame-independiente. spread_type==2
// reescala el "change" por 16.6667/frametime antes del original y restaura los valores
// ofuscados despues.
static void __fastcall patched_calculate_weapon_spread(void* ecx, void*, uint32_t frametime_param, uint8_t param_2)
{
    funny_value* inner_recovery = (funny_value*)((char*)ecx + 0x170);
    funny_value* inner_change   = (funny_value*)((char*)ecx + 0x17c);
    funny_value* outer_recovery = (funny_value*)((char*)ecx + 0x1ac);
    funny_value* outer_change   = (funny_value*)((char*)ecx + 0x1b8);
    uint32_t spread_type = *(uint32_t*)((char*)ecx + 0x148);

    uint32_t orig_inner_recovery = get_funny_value(inner_recovery);
    uint32_t orig_outer_recovery = get_funny_value(outer_recovery);
    uint32_t orig_inner_change   = get_funny_value(inner_change);
    uint32_t orig_outer_change   = get_funny_value(outer_change);

    if (spread_type == 2 && frametime_param != 0) {
        const double orig_fixed_frametime = 16.666666666666668;
        float ratio = (float)(orig_fixed_frametime / (frametime_param * 1.0));
        float new_inner = *(float*)&orig_inner_change * ratio;
        float new_outer = *(float*)&orig_outer_change * ratio;
        set_funny_value(inner_change, (uint32_t*)&new_inner);
        set_funny_value(outer_change, (uint32_t*)&new_outer);
    }

    orig_calculate_weapon_spread(ecx, frametime_param, param_2);

    set_funny_value(inner_recovery, &orig_inner_recovery);
    set_funny_value(outer_recovery, &orig_outer_recovery);
    set_funny_value(inner_change, &orig_inner_change);
    set_funny_value(outer_change, &orig_outer_change);
}

static void __fastcall patched_move_actor_by(void* ecx, void* edx, float deltaX, float deltaY, float deltaZ)
{
    actor_ctx* ctx = fetch_actor_ctx();
    float adjustedY = deltaY;

    if (!ctx) {
        orig_move_actor_by(ecx, deltaX, adjustedY, deltaZ);
        return;
    }

    static float exoAccumulatedTime = 0.0f;
    static bool wasAirborne = false;
    static bool plasmaDropFirstFrame = true;

    void* caller = _ReturnAddress();

    // el sitio "en el aire"; el de suelo (0x00526F0E) no se toca
    if (caller == (void*)S4(0x00527467)) {
        bool airborne = false;
        bool wasFlyingBefore = wasAirborne;
        bool flyEvade = (ctx->actor_state == 0x0B || ctx->actor_state == 0x0C) && wasFlyingBefore;

        if (ctx->actor_state == 31) airborne = true;
        if ((ctx->actor_state == 39 || ctx->actor_state == 25) &&
            (ctx->actor_substate_2 & 0xffff) == 0x02ff) airborne = true;
        if (ctx->actor_state == 4 && wasAirborne) airborne = true;
        if (flyEvade) airborne = true;
        wasAirborne = airborne;

        if (airborne && deltaY > 0.0001f) {
            const float baseFrameTime = 16.666666f;
            float scale = (baseFrameTime / frametime);
            if (frametime < baseFrameTime) {
                float frameRatio = (baseFrameTime - frametime) / baseFrameTime;
                scale *= (1.0f - 0.4f * frameRatio);
            }
            adjustedY = deltaY * scale;
            if (flyEvade)
                adjustedY *= flight_evade_up_multiplier;
        }

        if (ctx->actor_state == 63 && deltaY > 0.0f) {
            float ratio = 17.0f / frametime;
            if (frametime <= 13.0f)       adjustedY = deltaY / (ratio / (1.0f / 3.75f)) * ratio;
            else if (frametime >= 33.0f)  adjustedY = deltaY / (ratio / 4.0f) * ratio;
            else if (frametime >= 28.0f)  adjustedY = deltaY / (ratio / 3.0f) * ratio;
            else if (frametime >= 25.0f)  adjustedY = deltaY / (ratio / 2.25f) * ratio;
            else if (frametime >= 22.0f)  adjustedY = deltaY / (ratio / 2.0f) * ratio;
            else if (frametime >= 19.0f)  adjustedY = deltaY / (ratio / 1.75f) * ratio;
            else if (frametime >= 18.0f)  adjustedY = deltaY / (ratio / 1.5f) * ratio;
            exoAccumulatedTime += frametime;
            adjustedY *= exo_jump_multiplier;
        }
        else {
            exoAccumulatedTime = 0.0f;
        }

        if (ctx->actor_state == 45) {
            float drop_diff = set_drop_val + 50000.0f;
            if (drop_diff < 0.0f) drop_diff = -drop_diff;
            if (drop_diff < 1.0f && deltaY < -50.0f) {
                if (plasmaDropFirstFrame) {
                    adjustedY = -850.0f;
                    plasmaDropFirstFrame = false;
                }
                else {
                    adjustedY = 0.0f;
                }
            }
        }
        else {
            plasmaDropFirstFrame = true;
        }

        if (!airborne && ctx->actor_state != 63 && ctx->actor_state != 45 && deltaY > 0.0001f)
            adjustedY = deltaY * normal_jump_height_multiplier;
    }
    else {
        exoAccumulatedTime = 0.0f;
    }

    orig_move_actor_by(ecx, deltaX, adjustedY, deltaZ);
}

static void __fastcall patched_game_tick(void* ecx, void* edx)
{
    game_context* ctx = fetch_game_context();
    if (!ctx) return;


    bool should_limit = ctx->fps_limiter_toggle != 0;

    if (should_limit && max_framerate > 0) {
        static LARGE_INTEGER freq = { 0 };
        static LARGE_INTEGER last_tick = { 0 };
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        if (last_tick.QuadPart != 0) {
            double target_frametime_sec = 1.0 / max_framerate;
            LONGLONG target_ticks = (LONGLONG)(target_frametime_sec * freq.QuadPart);
            double elapsed_sec = double(now.QuadPart - last_tick.QuadPart) / freq.QuadPart;
            while (elapsed_sec < target_frametime_sec) {
                double remaining = target_frametime_sec - elapsed_sec;
                if (remaining > 0.001 && pNtDelayExecution) {
                    LARGE_INTEGER li;
                    li.QuadPart = -(LONGLONG)((remaining - 0.0006) * 10000000.0);
                    pNtDelayExecution(FALSE, &li);
                }
                QueryPerformanceCounter(&now);
                elapsed_sec = double(now.QuadPart - last_tick.QuadPart) / freq.QuadPart;
            }
            last_tick.QuadPart += target_ticks;
            if (now.QuadPart - last_tick.QuadPart > target_ticks)
                last_tick = now;
        }
        else {
            last_tick = now;
        }
    }

    uint8_t saved_fps_limiter = ctx->fps_limiter_toggle;
    ctx->fps_limiter_toggle = 0;
    orig_game_tick(ecx);
    ctx->fps_limiter_toggle = saved_fps_limiter;

    static LARGE_INTEGER freq = { 0 };
    static LARGE_INTEGER last_frame = { 0 };
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (last_frame.QuadPart != 0)
        frametime = (float)(now.QuadPart - last_frame.QuadPart) * 1000.0f / (float)freq.QuadPart;
    last_frame = now;

    const float orig_speed_dampener = 0.015f;
    const float orig_fixed_frametime = 16.666666f;
    float new_speed_dampener = frametime * orig_speed_dampener / orig_fixed_frametime;
    speed_dampeners[3] = new_speed_dampener;
    speed_dampeners[4] = new_speed_dampener;
    speed_dampeners[8] = new_speed_dampener;

    UpdateJumpMultiplierByFPS();
}

static void hook_move_actor_by() {
    orig_move_actor_by = (move_actor_by_t)S4(0x0051c2f0);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_move_actor_by, (PVOID)patched_move_actor_by);
    DetourTransactionCommit();
}

static void hook_game_tick() {
    orig_game_tick = (game_tick_t)S4(0x00871970);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_game_tick, (PVOID)patched_game_tick);
    DetourTransactionCommit();
}

static void hook_fun_005e4020() {
    orig_fun_005e4020 = (fun_005e4020_t)S4(0x005e4020);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_fun_005e4020, (PVOID)patched_fun_005e4020);
    DetourTransactionCommit();
}

static void hook_fov_update() {
    orig_fov_update = (fov_consumer_t)S4(0x00766000);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_fov_update, (PVOID)patched_fov_update);
    DetourTransactionCommit();
}

static void hook_calculate_weapon_spread() {
    orig_calculate_weapon_spread = (calc_spread_t)S4(0x0058c800);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_calculate_weapon_spread, (PVOID)patched_calculate_weapon_spread);
    DetourTransactionCommit();
}

void patch_min_frametime(double min_frametime) {
    double* min_frametime_const = (double*)S4(0x013d33a0);
    DWORD oldProtect;
    VirtualProtect(min_frametime_const, sizeof(double), PAGE_EXECUTE_READWRITE, &oldProtect);
    *min_frametime_const = min_frametime;
    VirtualProtect(min_frametime_const, sizeof(double), oldProtect, &oldProtect);
}

static void redirect_speed_dampeners() {
    const uint8_t value[] = { 0x8f, 0xc2, 0x75, 0x3c };
    for (int i = 0; i < 9; i++)
        memcpy(&speed_dampeners[i], value, sizeof(value));
    uint32_t* patch_location = nullptr;
    patch_location = (uint32_t*)S4(0x00563c0e); *patch_location = (uint32_t)&speed_dampeners[0];
    patch_location = (uint32_t*)S4(0x007b063d); *patch_location = (uint32_t)&speed_dampeners[1];
    patch_location = (uint32_t*)S4(0x007b06aa); *patch_location = (uint32_t)&speed_dampeners[2];
    patch_location = (uint32_t*)S4(0x007b1204); *patch_location = (uint32_t)&speed_dampeners[3];
    patch_location = (uint32_t*)S4(0x007b120c); *patch_location = (uint32_t)&speed_dampeners[4];
    patch_location = (uint32_t*)S4(0x007b1973); *patch_location = (uint32_t)&speed_dampeners[5];
    patch_location = (uint32_t*)S4(0x007b19a9); *patch_location = (uint32_t)&speed_dampeners[6];
    patch_location = (uint32_t*)S4(0x007b1edc); *patch_location = (uint32_t)&speed_dampeners[7];
    patch_location = (uint32_t*)S4(0x007b2363); *patch_location = (uint32_t)&speed_dampeners[8];
}

static void prepare_nt_timer() {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    if (!nt) return;
    pNtDelayExecution       = (NtDelayExecution_t)GetProcAddress(nt, "NtDelayExecution");
    pNtQueryTimerResolution = (NtQueryTimerResolution_t)GetProcAddress(nt, "NtQueryTimerResolution");
    pNtSetTimerResolution   = (NtSetTimerResolution_t)GetProcAddress(nt, "NtSetTimerResolution");
    if (!pNtQueryTimerResolution || !pNtSetTimerResolution) return;
    ULONG mn = 0, mx = 0, cur = 0;
    pNtQueryTimerResolution(&mn, &mx, &cur);
    for (int i = 0; i < 10; i++) {
        pNtSetTimerResolution(mx, TRUE, &cur);
        if (cur == mx) break;
    }
}

static DWORD WINAPI main_thread(LPVOID) {
    LoadConfig();
    prepare_nt_timer();
    patch_min_frametime(1.0 / max_framerate);
    redirect_speed_dampeners();
    hook_game_tick();
    hook_move_actor_by();
    hook_fun_005e4020();
    hook_fov_update();
    hook_calculate_weapon_spread();
    return 0;
}

void StartGameEnhancements() {
    CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr);
}
