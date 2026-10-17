#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>

// ============================================================================
// S4 League - Immersive Dark Mode Window Theme (Windows 10 / 11)
// ============================================================================

bool SystemUsesDarkMode()
{
    HKEY hKey = nullptr;
    DWORD appsUseLight = 1;  // default to light if the key is missing
    DWORD size = sizeof(appsUseLight);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
            reinterpret_cast<BYTE*>(&appsUseLight), &size);
        RegCloseKey(hKey);
    }
    return appsUseLight == 0;
}

extern "C" void ApplySystemTheme(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;

    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;

    using FnDwmSetWindowAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<FnDwmSetWindowAttribute>(
        GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (fn) {
        BOOL useDark = SystemUsesDarkMode() ? TRUE : FALSE;
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE on Win10 1903+ and Win11.
        // 19 = same attribute on Win10 builds 18362..18985 before it was renumbered.
        fn(hwnd, 20, &useDark, sizeof(useDark));
        fn(hwnd, 19, &useDark, sizeof(useDark));
    }
    FreeLibrary(dwm);
}

static decltype(&CreateWindowExA) TrueCreateWindowExA = CreateWindowExA;

static HWND WINAPI Hook_CreateWindowExA(
    DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
    HINSTANCE hInstance, LPVOID lpParam)
{
    HWND hwnd = TrueCreateWindowExA(
        dwExStyle, lpClassName, lpWindowName, dwStyle,
        X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (hwnd) {
        ApplySystemTheme(hwnd);
    }
    return hwnd;
}

void InstallDarkModeWindowFix()
{
    static bool installed = false;
    if (installed) return;
    installed = true;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)TrueCreateWindowExA, Hook_CreateWindowExA);
    DetourTransactionCommit();
}
