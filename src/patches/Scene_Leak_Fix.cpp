#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <cstring>
#include <cstdint>

namespace
{
    const bool   ANTIRELOAD = true;
    const DWORD  ANTIRELOAD_MS = 500;

    const uintptr_t SCENE_LOADER_ADDR = 0x01CB8500;
    const uintptr_t SET_SCENE_ADDR    = 0x01CCCF50;

    std::string Normalize(const char* path)
    {
        std::string s(path ? path : "");
        for (char& c : s)
        {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        }
        return s;
    }

    typedef int (__fastcall* tVtblCall)(void* thisptr, void* edx);
    void AddRef(void* obj)
    {
        if (!obj) return;
        void** vtbl = *(void***)obj;
        ((tVtblCall)vtbl[1])(obj, nullptr);
    }

    typedef void* (__fastcall* tLoadScene)(void* thisptr, void* edx, char* path, void* a2, void* a3);
    tLoadScene oLoadScene = nullptr;

    const size_t CACHE_CAP = 8192;
    std::unordered_map<std::string, void*> g_cache;
    std::mutex g_cacheMutex;

    bool EndsWithCI(const std::string& s, const char* ext)
    {
        size_t n = s.size(), m = strlen(ext);
        return n >= m && s.compare(n - m, m, ext) == 0;
    }
    bool ShouldIntern(const std::string& key)
    {
        static const char* kImageExts[] = {
            ".dds", ".tga", ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tif", ".tiff", ".dxt"
        };
        for (const char* e : kImageExts)
            if (EndsWithCI(key, e)) return true;
        return false;
    }

    void* __fastcall hkLoadScene(void* thisptr, void* edx, char* path, void* a2, void* a3)
    {
        if (!path)
            return oLoadScene(thisptr, edx, path, a2, a3);

        const std::string key = Normalize(path);
        if (!ShouldIntern(key))
            return oLoadScene(thisptr, edx, path, a2, a3);

        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            auto it = g_cache.find(key);
            if (it != g_cache.end() && it->second)
            {
                AddRef(it->second);
                return it->second;
            }
        }
        void* obj = oLoadScene(thisptr, edx, path, a2, a3);
        if (obj)
        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            if (g_cache.size() < CACHE_CAP && g_cache.find(key) == g_cache.end())
            {
                AddRef(obj);
                g_cache[key] = obj;
            }
        }
        return obj;
    }

    typedef void (__thiscall* tSetScene)(void* thisptr, unsigned slot, char* path);
    tSetScene oSetScene = nullptr;

    struct SlotState { std::string path; DWORD time; };
    SlotState g_slot[10];
    std::mutex g_slotMutex;

    void __fastcall hkSetScene(void* thisptr, void* edx, unsigned slot, char* path)
    {
        if (ANTIRELOAD && path && slot < 10)
        {
            const std::string key = Normalize(path);
            const DWORD now = GetTickCount();
            std::lock_guard<std::mutex> lk(g_slotMutex);
            SlotState& s = g_slot[slot];
            if (s.path == key && (now - s.time) < ANTIRELOAD_MS)
                return;
            s.path = key;
            s.time = now;
        }
        oSetScene(thisptr, slot, path);
    }
}

void InstallSceneLeakFix()
{
    oLoadScene = (tLoadScene)SCENE_LOADER_ADDR;
    oSetScene  = (tSetScene)SET_SCENE_ADDR;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oLoadScene, hkLoadScene);
    if (ANTIRELOAD) DetourAttach(&(PVOID&)oSetScene, hkSetScene);
    DetourTransactionCommit();
}
