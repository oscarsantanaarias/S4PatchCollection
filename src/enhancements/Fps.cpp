#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <detours.h>
#include <stdint.h>
#include <intrin.h>
#include <cstring>

#pragma intrinsic(_ReturnAddress)
#pragma comment(lib, "detours.lib")

// no static: el overlay maneja estos knobs en vivo, ver overlay/Overlay.cpp
int max_framerate = 144;      // cap base (busy-loop preciso)
int full_framerate = 9940;    // F5 sube a esto, siempre con busy-loop
bool fps_unlocked = false;    // F5 togglea entre los dos caps
float physics_hz = 60.0f;     // la fisica corre a este Hz, el render va libre
float field_of_view = 90.0f;  // target del FOV del jugador, en grados
float applied_fov = 90.0f;    // valor suavizado que se aplica de verdad

float normal_jump_height_multiplier = 1.100f;
float fly_multiplier = 1.807f;
float fly_evade_up_multiplier = 1.8f;

static float frametime = 16.666666f;

// El estado del actor vive en un global. En lobby/preview es invalido (0xFFFFFFFF), y eso
// se usa para saber si estamos en partida.
static const uintptr_t ACTOR_STATE   = 0x010E1648;
static const uintptr_t FETCH_CONTEXT = 0x004054C0;
static const uintptr_t GAME_TICK     = 0x007D8790;
static const uintptr_t MOVE_ACTOR_BY = 0x00456630;

// Wrapper de D3DXMatrixPerspectiveFovLH(pOut, fovY_rad, aspect, zn, zf): punto TERMINAL
// de la proyeccion, lo que llega aca ES el FOV que se renderiza y nadie lo reescribe
// despues. El campo de la camara era solo una cache y otro writer la pisaba, de ahi el
// "va y viene".
static const uintptr_t PERSP_FOV     = 0x00B83FF0;

// Fuente de tiempo del engine: ~15ms de resolucion, o sea delta grueso a fps alto.
static const uintptr_t TIME_GET      = 0x00BAEC50;

// SetDrop, el metodo que el lua de cada arma usa para fijar su caida. Hay uno por clase
// de estado: CAttackState y su version extendida. Los dos guardan valor + vec3, cada uno
// en su offset. De los 208 scripts, el unico con +-50000 es el plasma; bat y katana usan
// +-20000, doublesword +-10000 y countersword +-4000. O sea el drop mas grande es el del
// plasma, y de ahi sale el pico de su stun sin numeros magicos.
static const uintptr_t SET_DROP_BASE = 0x0053B750;   // CAttackState::SetDrop
static const uintptr_t SET_DROP_EX   = 0x0053F740;   // CAttackStateEx::SetDrop

static volatile float largest_drop = 50000.0f;       // fallback: lo que trae el plasma

// FOV normal del juego (correr suma ~+20 sobre esto). Se usa offset ADITIVO para
// preservar ese +20 y la transicion suave nativa, en vez de aplanar a un valor fijo.
static const float BASE_NORMAL_FOV = 60.0f;
static const float FOV_MIN = 40.0f;
static const float FOV_MAX = 100.0f;

// "en juego": la fisica del actor solo corre en partida, en el lobby el preview es pura
// animacion. Se marca su ultimo tick y el FOV solo se toca si es reciente.
static volatile DWORD g_lastMoveTick = 0;

struct game_context {
    uint8_t unknown[0x1D];
    uint8_t fps_limiter_toggle;
};

typedef game_context* (__cdecl* fetch_context_t)(void);
static fetch_context_t fetch_game_context = (fetch_context_t)FETCH_CONTEXT;

typedef void(__fastcall* game_tick_t)(void*);
static game_tick_t orig_game_tick = (game_tick_t)GAME_TICK;

typedef void(__thiscall* move_actor_by_t)(void*, float, float, float);
static move_actor_by_t orig_move_actor_by = (move_actor_by_t)MOVE_ACTOR_BY;

typedef void(__thiscall* persp_t)(void*, uint32_t, uint32_t, uint32_t, uint32_t);
static persp_t orig_persp = (persp_t)PERSP_FOV;

typedef DWORD(__fastcall* timeget_t)(int);
static timeget_t orig_timeget = (timeget_t)TIME_GET;

typedef void(__fastcall* setdrop_t)(void*, void*, unsigned, unsigned*);
static setdrop_t orig_setdrop_base = (setdrop_t)SET_DROP_BASE;
static setdrop_t orig_setdrop_ex   = (setdrop_t)SET_DROP_EX;

static LARGE_INTEGER g_qpcFreq = { 0 };
static LARGE_INTEGER g_qpcStart = { 0 };
static DWORD g_timeBase = 0;
static volatile LONG g_timeInit = 0;

uint32_t ActorState()
{
    uint32_t state = *(uint32_t*)ACTOR_STATE;
    if (state == 0xFFFFFFFFu || state > 100)
        state = 5;
    return state;
}

// Se reemplaza por ms de alta resolucion (QPC) pero alineada al epoch del timer original
// (base = original - qpc en la primera llamada), asi es continua y no hay salto: el delta
// queda fino y los timers de piso/gravedad no se confunden. Ademas cuantiza el tiempo a
// pasos de la fisica, o sea el juego siempre ve el delta de physics_hz -> la velocidad es
// identica a cualquier FPS de render y el pullback del skin pasa physics_hz veces por
// segundo en vez de una por frame.
static DWORD __fastcall patched_timeget(int param_1)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double realMs = (double)(now.QuadPart - g_qpcStart.QuadPart) * 1000.0 / (double)g_qpcFreq.QuadPart;
    double stepMs = 1000.0 / (double)physics_hz;
    long long steps = (long long)(realMs / stepMs);
    DWORD gameMs = (DWORD)((double)steps * stepMs);
    if (InterlockedCompareExchange(&g_timeInit, 1, 0) == 0)
        g_timeBase = orig_timeget(param_1) - gameMs;
    return g_timeBase + gameMs;
}

// Se queda con la caida mas grande que declara el lua, que es la del plasma. Corre una vez
// por arma al crear los estados, no por golpe.
static void NoteDrop(unsigned value, unsigned* vec)
{
    float mag = 0.0f;
    if (vec)
    {
        for (int i = 0; i < 3; i++)
        {
            float f = *(float*)&vec[i];
            if (f < 0.0f) f = -f;
            if (f > mag) mag = f;
        }
    }
    float v = *(float*)&value;
    if (v < 0.0f) v = -v;
    if (v > mag) mag = v;

    if (mag > largest_drop)
        largest_drop = mag;
}

static void __fastcall patched_setdrop_base(void* ecx, void* edx, unsigned value, unsigned* vec)
{
    NoteDrop(value, vec);
    orig_setdrop_base(ecx, edx, value, vec);
}

static void __fastcall patched_setdrop_ex(void* ecx, void* edx, unsigned value, unsigned* vec)
{
    NoteDrop(value, vec);
    orig_setdrop_ex(ecx, edx, value, vec);
}

// Busy-loop preciso (spin apretado, sin Sleep) gateado por el toggle del juego, y el
// toggle se guarda y se restaura en vez de dejarlo en 0.
static void __fastcall patched_game_tick(void* ecx)
{
    game_context* ctx = fetch_game_context();
    bool should_limit = (ctx != nullptr) && (ctx->fps_limiter_toggle != 0);
    int cap = fps_unlocked ? full_framerate : max_framerate;

    if (should_limit && cap > 0) {
        static LARGE_INTEGER freq{};
        static LARGE_INTEGER last_tick{};
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (last_tick.QuadPart != 0) {
            double target = 1.0 / cap;
            double elapsed = double(now.QuadPart - last_tick.QuadPart) / double(freq.QuadPart);
            while (elapsed < target) {
                QueryPerformanceCounter(&now);
                elapsed = double(now.QuadPart - last_tick.QuadPart) / double(freq.QuadPart);
            }
        }
        last_tick = now;
    }

    uint8_t saved = (ctx != nullptr) ? ctx->fps_limiter_toggle : 0;
    if (ctx) ctx->fps_limiter_toggle = 0;
    orig_game_tick(ecx);
    if (ctx) ctx->fps_limiter_toggle = saved;

    static LARGE_INTEGER freq2{};
    static LARGE_INTEGER last_frame{};
    if (freq2.QuadPart == 0) QueryPerformanceFrequency(&freq2);
    LARGE_INTEGER now2;
    QueryPerformanceCounter(&now2);
    if (last_frame.QuadPart != 0)
        frametime = (float)(now2.QuadPart - last_frame.QuadPart) * 1000.0f / (float)freq2.QuadPart;
    last_frame = now2;
}

static void __fastcall patched_move_actor_by(void* ecx, void* edx, float x, float y, float z)
{
    float adjustedY = y;
    const uint32_t state = ActorState();
    const uintptr_t caller = (uintptr_t)_ReturnAddress();

    // los dos sitios de la fisica del actor; el resto de los callers no se toca
    const bool validCaller = (caller == 0x00454479 || caller == 0x00454782);
    if (validCaller)
        g_lastMoveTick = GetTickCount();

    static bool flewRecently = false;
    static bool plasmaFirst = true;

    if (validCaller) {
        // Combo aereo: 31=volar con alas, 40=cubrirse con CS, 11/12=evade. El boost del
        // evade requiere haber VOLADO antes, persiste por el combo y se resetea al pisar,
        // asi el cover sin volar + evade no sube. (39/25 = recargar/disparar saltando, no
        // es vuelo.)
        const bool flying = (state == 31);
        const bool csCover = (state == 40);
        const bool evade = (state == 0x0B || state == 0x0C);
        const bool inAirCombo = flying || csCover || evade;
        if (flying)      flewRecently = true;
        if (!inAirCombo) flewRecently = false;
        const bool flyEvade = evade && flewRecently;

        // multiplicadores planos (sin frametime), o sea altura constante a cualquier FPS
        if (state == 45) {                          // stun-drop del plasma
            // El pico sale del propio valor del lua: drop / Hz de fisica. Con el 50000 del
            // plasma a 60 Hz da 833, que es de donde venia el 850 hardcodeado.
            const float spike = -(largest_drop / physics_hz);
            if (y < -50.0f) {
                adjustedY = plasmaFirst ? spike : 0.0f;
                plasmaFirst = false;
            }
        }
        else {
            plasmaFirst = true;
            if (flyEvade && y > 0.0001f)     adjustedY = y * fly_evade_up_multiplier;
            else if (flying && y > 0.0001f)  adjustedY = y * fly_multiplier;
            else if (csCover)                { /* neutral, no toca Y */ }
            else if (y > 0.0001f)            adjustedY = y * normal_jump_height_multiplier;
            else if (y < -0.0001f)           adjustedY = y * 0.75f;
        }
    }

    orig_move_actor_by(ecx, x, adjustedY, z);
}

// fovY llega en RADIANES. El offset se aplica con RAMPA en los bordes para que no haya
// salto duro al cruzar la banda (scope del sniper <-> normal):
//   factor 0 abajo de 40, sube a 1 en 40..52, 1 en 52..82, baja a 0 en 82..90, 0 arriba.
// Asi el scope (~20-30, factor 0) queda zoomeado, normal 60 y correr 80 (factor 1) pasan a
// 90/110, y la UI (~90) no se toca.
static void __fastcall patched_persp(void* ecx, void* edx, uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4)
{
    // Solo en partida. En lobby el actor global es invalido, asi que se deja el FOV crudo
    // al instante y no hay ese "chico -> grande" de unos segundos.
    const uint32_t rs = *(uint32_t*)ACTOR_STATE;
    const bool inGame = (rs != 0xFFFFFFFFu && rs <= 100) && (GetTickCount() - g_lastMoveTick < 1500);
    if (!inGame) { orig_persp(ecx, p1, p2, p3, p4); return; }

    float fovY;
    memcpy(&fovY, &p1, sizeof(float));
    float deg = fovY * 57.29577951f;

    float factor;
    if (deg <= 40.0f || deg >= 90.0f) factor = 0.0f;
    else if (deg < 52.0f)             factor = (deg - 40.0f) / 12.0f;
    else if (deg <= 82.0f)            factor = 1.0f;
    else                              factor = (90.0f - deg) / 8.0f;

    if (factor > 0.0f) {
        deg = deg + (applied_fov - BASE_NORMAL_FOV) * factor;
        fovY = deg * 0.01745329252f;
        memcpy(&p1, &fovY, sizeof(float));
    }
    orig_persp(ecx, p1, p2, p3, p4);
}

// PgUp/PgDn mueven el target del FOV y F5 togglea el cap. El suavizado corre en este hilo
// a tick fijo, o sea independiente del framerate, y el cambio se ve fluido.
static DWORD WINAPI hotkeys(LPVOID)
{
    bool up = false, dn = false, unlock = false;
    for (;;) {
        const bool nu = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
        const bool nd = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
        const bool nunlock = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (nu && !up) field_of_view = min(field_of_view + 5.0f, FOV_MAX - 1.0f);
        if (nd && !dn) field_of_view = max(field_of_view - 5.0f, FOV_MIN + 1.0f);
        if (nunlock && !unlock) fps_unlocked = !fps_unlocked;
        up = nu; dn = nd; unlock = nunlock;

        applied_fov += (field_of_view - applied_fov) * 0.20f;
        Sleep(16);
    }
}

static DWORD WINAPI main_thread(LPVOID)
{
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_qpcStart);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)orig_game_tick, patched_game_tick);
    DetourAttach(&(PVOID&)orig_move_actor_by, patched_move_actor_by);
    DetourAttach(&(PVOID&)orig_persp, patched_persp);
    DetourAttach(&(PVOID&)orig_timeget, patched_timeget);
    DetourAttach(&(PVOID&)orig_setdrop_base, patched_setdrop_base);
    DetourAttach(&(PVOID&)orig_setdrop_ex, patched_setdrop_ex);
    DetourTransactionCommit();

    CreateThread(nullptr, 0, hotkeys, nullptr, 0, nullptr);
    return 0;
}

void StartGameEnhancements()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    CreateThread(nullptr, 0, main_thread, nullptr, 0, nullptr);
}
