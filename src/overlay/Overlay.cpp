#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <detours.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "detours.lib")

// knobs de enhancements/Fps.cpp
extern int max_framerate;
extern int full_framerate;
extern bool fps_unlocked;
extern float physics_hz;
extern float field_of_view;
extern float applied_fov;
uint32_t ActorState();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    const char* CONF = "conf.json";

    // A mano en vez de traer json.hpp: son cuatro numeros en un objeto plano. Si algun
    // dia hay algo anidado, ahi si vale la dependencia.
    int ReadInt(const std::string& doc, const char* key, int fallback)
    {
        const std::string needle = std::string("\"") + key + "\"";
        size_t k = doc.find(needle);
        if (k == std::string::npos) return fallback;
        k = doc.find(':', k + needle.size());
        if (k == std::string::npos) return fallback;
        return atoi(doc.c_str() + k + 1);
    }

    void Save()
    {
        FILE* f = fopen(CONF, "w");
        if (!f) return;
        fprintf(f,
            "{\n"
            "  \"max_framerate\": %d,\n"
            "  \"field_of_view\": %d,\n"
            "  \"physics_hz\": %d,\n"
            "  \"fps_unlocked\": %d\n"
            "}\n",
            max_framerate, (int)field_of_view, (int)physics_hz, fps_unlocked ? 1 : 0);
        fclose(f);
    }

    void Load()
    {
        FILE* f = fopen(CONF, "rb");
        if (!f) { Save(); return; }          // primera vez: se escriben los defaults
        std::string doc;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) doc.append(buf, n);
        fclose(f);

        max_framerate = ReadInt(doc, "max_framerate", max_framerate);
        field_of_view = (float)ReadInt(doc, "field_of_view", (int)field_of_view);
        physics_hz    = (float)ReadInt(doc, "physics_hz", (int)physics_hz);
        fps_unlocked  = ReadInt(doc, "fps_unlocked", fps_unlocked ? 1 : 0) != 0;
        applied_fov   = field_of_view;       // sin rampa al arrancar
    }

    // No se busca la ventana por clase ni por titulo, eso cambia entre builds. Estamos
    // dentro del proceso, asi que alcanza con tomar nuestra propia ventana visible.
    BOOL CALLBACK PickWindow(HWND h, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(h)) return TRUE;
        if (GetWindow(h, GW_OWNER)) return TRUE;      // dialogos y tool windows fuera

        RECT r{};
        GetClientRect(h, &r);
        if (r.right < 320 || r.bottom < 240) return TRUE;   // splash fuera

        *(HWND*)out = h;
        return FALSE;
    }

    HWND FindGameWindow()
    {
        HWND h = nullptr;
        EnumWindows(PickWindow, (LPARAM)&h);
        return h;
    }

    typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
    typedef HRESULT(__stdcall* Reset_t)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);

    EndScene_t oEndScene = nullptr;
    Reset_t    oReset = nullptr;
    WNDPROC    oWndProc = nullptr;
    HWND       g_window = nullptr;
    bool       g_ready = false;
    bool       g_show = false;      // apagado hasta que se toque INSERT

    LRESULT __stdcall WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        // solo se roba el input mientras el panel esta abierto
        if (g_show && ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l))
            return true;
        return CallWindowProc(oWndProc, hwnd, msg, w, l);
    }

    const ImVec4 ACCENT     = ImVec4(0.00f, 0.85f, 0.70f, 1.00f);
    const ImVec4 ACCENT_DIM = ImVec4(0.00f, 0.55f, 0.46f, 1.00f);

    void Style()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 0.0f;
        s.ChildRounding     = 0.0f;
        s.FrameRounding     = 0.0f;
        s.PopupRounding     = 0.0f;
        s.ScrollbarRounding = 0.0f;
        s.WindowBorderSize  = 1.0f;
        s.FrameBorderSize   = 1.0f;
        s.WindowPadding     = ImVec2(16, 14);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(8, 9);

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text]         = ImVec4(0.86f, 0.90f, 0.92f, 1.00f);
        c[ImGuiCol_TextDisabled] = ACCENT_DIM;
        c[ImGuiCol_WindowBg]     = ImVec4(0.04f, 0.05f, 0.06f, 0.94f);
        c[ImGuiCol_Border]       = ACCENT_DIM;
        c[ImGuiCol_Separator]    = ImVec4(0.00f, 0.35f, 0.30f, 1.00f);
    }

    void Heading(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ACCENT);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // valor con [-] y [+] a los costados, un paso por click
    bool Stepper(const char* id, int* v, int lo, int hi, const char* fmt)
    {
        bool changed = false;
        ImGui::PushID(id);
        const float btn = ImGui::GetFrameHeight();
        if (ImGui::Button("-", ImVec2(btn, btn))) { (*v)--; changed = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btn - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::SliderInt("##v", v, lo, hi, fmt)) changed = true;
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2(btn, btn))) { (*v)++; changed = true; }
        ImGui::PopID();
        if (*v < lo) *v = lo;
        if (*v > hi) *v = hi;
        return changed;
    }

    void Draw()
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        // clavado al centro cada frame, y NoMove para que se quede ahi
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin("##s4fixes", nullptr, flags))
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ACCENT);
            ImGui::TextUnformatted("S4  //  FIXES");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            bool dirty = false;

            Heading("FRAMERATE");
            if (Stepper("fps", &max_framerate, 30, 1000, "%d FPS")) dirty = true;
            if (ImGui::Checkbox("F5  quitar el cap", &fps_unlocked)) dirty = true;

            ImGui::Spacing();
            Heading("FIELD OF VIEW");

            // El hook de la matriz aplica un offset sobre el FOV del juego, asi que el
            // +20 del sprint y su transicion suave se conservan.
            int fov = (int)field_of_view;
            if (Stepper("fov", &fov, 41, 99, "%d deg")) { field_of_view = (float)fov; dirty = true; }
            ImGui::TextDisabled("aplicado %.1f   (PgUp / PgDn)", applied_fov);

            ImGui::Spacing();
            Heading("PHYSICS");
            int hz = (int)physics_hz;
            if (Stepper("hz", &hz, 30, 240, "%d Hz")) { physics_hz = (float)hz; dirty = true; }
            ImGui::TextDisabled("actor state %u", ActorState());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ACCENT_DIM);
            ImGui::TextUnformatted("INSERT  cerrar");
            ImGui::PopStyleColor();

            if (dirty) Save();
        }
        ImGui::End();
    }

    HRESULT __stdcall hkReset(LPDIRECT3DDEVICE9 dev, D3DPRESENT_PARAMETERS* pp)
    {
        if (g_ready) ImGui_ImplDX9_InvalidateDeviceObjects();
        HRESULT hr = oReset(dev, pp);
        if (SUCCEEDED(hr) && g_ready) ImGui_ImplDX9_CreateDeviceObjects();
        return hr;
    }

    HRESULT __stdcall hkEndScene(LPDIRECT3DDEVICE9 dev)
    {
        if (!g_ready)
        {
            g_window = FindGameWindow();
            if (!g_window) return oEndScene(dev);

            ImGui::CreateContext();
            Style();
            ImGui_ImplWin32_Init(g_window);
            ImGui_ImplDX9_Init(dev);
            oWndProc = (WNDPROC)SetWindowLongPtr(g_window, GWLP_WNDPROC, (LONG_PTR)WndProc);
            Load();
            g_ready = true;
        }

        static bool wasDown = false;
        const bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (down && !wasDown)
        {
            g_show = !g_show;
            if (!g_show) Save();          // al cerrar, para que las hotkeys tambien queden
        }
        wasDown = down;

        if (!g_show) return oEndScene(dev);   // nada que dibujar, no molestamos

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Draw();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        return oEndScene(dev);
    }

    // Un device descartable solo para leer la vtable. Las addresses son por-proceso, no
    // por-device, asi que hookear esas entradas atrapa las llamadas que hace el juego.
    DWORD WINAPI InitThread(LPVOID)
    {
        while (!(g_window = FindGameWindow()))
            Sleep(100);

        IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) return 0;

        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = g_window;

        IDirect3DDevice9* dev = nullptr;
        if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)))
        {
            d3d->Release();
            return 0;
        }

        void** vt = *reinterpret_cast<void***>(dev);
        oEndScene = (EndScene_t)vt[42];
        oReset    = (Reset_t)vt[16];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)oEndScene, hkEndScene);
        DetourAttach(&(PVOID&)oReset, hkReset);
        DetourTransactionCommit();

        dev->Release();
        d3d->Release();
        return 0;
    }
}

void StartOverlay()
{
    CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
}
