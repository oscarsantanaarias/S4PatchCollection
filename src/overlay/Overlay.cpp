#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <detours.h>
#include <cstdio>
#include <string>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "detours.lib")

// live knobs, owned by Fps.cpp
extern int max_framerate;
extern int field_of_view;
extern int center_field_of_view;
extern int sprint_field_of_view;
void ApplyFrameRate();

// contadores de los patches, para verlos en vivo sin escribir nada a disco
extern "C" long FileExistsServed();
extern "C" long FileExistsDisk();
extern "C" long OomCaught();

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    const char* CONF = "conf.json";

    // Don't look the window up by class or title, those differ between builds.
    // We're inside the process, so just take our own visible top-level window.
    BOOL CALLBACK PickWindow(HWND h, LPARAM out)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        if (!IsWindowVisible(h)) return TRUE;
        if (GetWindow(h, GW_OWNER)) return TRUE;      // skip dialogs and tool windows

        RECT r{};
        GetClientRect(h, &r);
        if (r.right < 320 || r.bottom < 240) return TRUE;   // skip splash-sized stuff

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
    bool       g_show = false;      // off until INSERT

    // How far sprint sits above the base FOV. Tunable, so the three game states stay
    // derived from one knob instead of drifting apart.
    int g_sprintOffset = 20;
    const int CENTER_OFFSET = 6;

    void ApplyFov(int base)
    {
        field_of_view = base;
        center_field_of_view = base + CENTER_OFFSET;
        sprint_field_of_view = base + g_sprintOffset;
    }

    // Hand-rolled instead of pulling in json.hpp: a handful of numbers in a flat
    // object. Anything nested and it's worth the dependency, but this isn't.
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
            "  \"center_field_of_view\": %d,\n"
            "  \"sprint_field_of_view\": %d,\n"
            "  \"sprint_offset\": %d\n"
            "}\n",
            max_framerate, field_of_view, center_field_of_view,
            sprint_field_of_view, g_sprintOffset);
        fclose(f);
    }

    void Load()
    {
        FILE* f = fopen(CONF, "rb");
        if (!f) { Save(); return; }          // first run: write the defaults out
        std::string doc;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) doc.append(buf, n);
        fclose(f);

        max_framerate        = ReadInt(doc, "max_framerate", max_framerate);
        field_of_view        = ReadInt(doc, "field_of_view", field_of_view);
        center_field_of_view = ReadInt(doc, "center_field_of_view", center_field_of_view);
        sprint_field_of_view = ReadInt(doc, "sprint_field_of_view", sprint_field_of_view);
        g_sprintOffset       = ReadInt(doc, "sprint_offset", g_sprintOffset);
        ApplyFov(field_of_view);       // keep the three consistent with the offset
        ApplyFrameRate();
    }

    LRESULT __stdcall WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        // only steal input while the menu is up, otherwise the game gets nothing
        if (g_show && ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l))
            return true;
        return CallWindowProc(oWndProc, hwnd, msg, w, l);
    }

    const ImVec4 ACCENT     = ImVec4(0.00f, 0.85f, 0.70f, 1.00f);
    const ImVec4 ACCENT_DIM = ImVec4(0.00f, 0.55f, 0.46f, 1.00f);

    void Style()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 0.0f;      // hard edges read sharper than rounded ones
        s.ChildRounding     = 0.0f;
        s.FrameRounding     = 0.0f;
        s.GrabRounding      = 0.0f;
        s.PopupRounding     = 0.0f;
        s.ScrollbarRounding = 0.0f;
        s.WindowBorderSize  = 1.0f;
        s.FrameBorderSize   = 1.0f;
        s.WindowPadding     = ImVec2(16, 14);
        s.FramePadding      = ImVec2(10, 6);
        s.ItemSpacing       = ImVec2(8, 9);
        s.GrabMinSize       = 14.0f;

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text]           = ImVec4(0.86f, 0.90f, 0.92f, 1.00f);
        c[ImGuiCol_TextDisabled]   = ACCENT_DIM;
        c[ImGuiCol_WindowBg]       = ImVec4(0.04f, 0.05f, 0.06f, 0.94f);
        c[ImGuiCol_ChildBg]        = ImVec4(0.07f, 0.09f, 0.10f, 1.00f);
        c[ImGuiCol_Border]         = ACCENT_DIM;
        c[ImGuiCol_FrameBg]        = ImVec4(0.09f, 0.12f, 0.13f, 1.00f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.18f, 0.19f, 1.00f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.14f, 0.22f, 0.23f, 1.00f);
        c[ImGuiCol_Button]         = ImVec4(0.10f, 0.14f, 0.15f, 1.00f);
        c[ImGuiCol_ButtonHovered]  = ACCENT_DIM;
        c[ImGuiCol_ButtonActive]   = ACCENT;
        c[ImGuiCol_SliderGrab]     = ACCENT;
        c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 1.00f, 0.90f, 1.00f);
        c[ImGuiCol_Separator]      = ImVec4(0.00f, 0.35f, 0.30f, 1.00f);
        c[ImGuiCol_PlotHistogram]  = ACCENT;
    }

    // value with a [-] and [+] on either side, one step per click. Returns true the
    // frame it changes so the caller can apply and persist right away.
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

    void Heading(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ACCENT);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    void Draw()
    {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const ImVec2 size = ImVec2(380, 0);

        // pinned dead centre every frame, and NoMove so it stays there
        ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin("##s4settings", nullptr, flags))
        {
            bool dirty = false;

            ImGui::PushStyleColor(ImGuiCol_Text, ACCENT);
            ImGui::TextUnformatted("S4  //  CONFIG");
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();

            Heading("FRAMERATE");
            if (Stepper("fps", &max_framerate, 30, 1000, "%d FPS"))
            {
                ApplyFrameRate();   // the limiter is patched into memory, it won't reread this
                dirty = true;
            }

            ImGui::Spacing();
            Heading("FIELD OF VIEW");

            // One knob for the base, the other for how far sprint sits above it. The
            // hook picks between them per frame off what the game writes, so both are
            // live the moment they move.
            int fov = field_of_view;
            if (Stepper("fov", &fov, 50, 120, "%d deg")) { ApplyFov(fov); dirty = true; }

            if (Stepper("sprintoff", &g_sprintOffset, 0, 60, "sprint +%d")) { ApplyFov(field_of_view); dirty = true; }

            ImGui::TextDisabled("center %d   sprint %d", center_field_of_view, sprint_field_of_view);

            ImGui::Spacing();
            Heading("FIXES");

            const long served = FileExistsServed();
            const long disk   = FileExistsDisk();
            const long total  = served + disk;
            ImGui::TextDisabled("file lookups   %ld   (disco %ld, cache %ld)", total, disk, served);
            if (total > 0)
                ImGui::TextDisabled("ahorro         %.1f%%", 100.0 * served / total);
            ImGui::TextDisabled("oom fail-soft  %ld", OomCaught());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ACCENT_DIM);
            ImGui::TextUnformatted("INSERT  close");
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
        if (down && !wasDown) g_show = !g_show;
        wasDown = down;

        if (!g_show) return oEndScene(dev);   // nothing to draw, stay out of the way

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Draw();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        return oEndScene(dev);
    }

    // A throwaway device just to read the vtable. The addresses are per-process, not
    // per-device, so hooking these entries catches the calls the game makes on its own.
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
