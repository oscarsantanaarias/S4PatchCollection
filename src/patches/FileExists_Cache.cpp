#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <string>
#include <unordered_map>

namespace
{
    typedef bool(__cdecl* tFileExists)(const char*);
    tFileExists oFileExists = (tFileExists)0x01B7AFB0;

    std::unordered_map<std::string, bool> g_cache;
    SRWLOCK g_lock = SRWLOCK_INIT;

    volatile LONG g_served = 0;
    volatile LONG g_disk   = 0;

    void DumpStats()
    {
        char buf[128];
        wsprintfA(buf, "served_from_cache=%ld  disk_calls=%ld\r\n", g_served, g_disk);
        HANDLE h = CreateFileA("fileexists_cache_stats.txt", GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD w;
            WriteFile(h, buf, lstrlenA(buf), &w, nullptr);
            CloseHandle(h);
        }
    }

    bool IsReadOnlyResource(const char* p)
    {
        return (p[0] == 'R' || p[0] == 'r') &&
               (memcmp(p, "Resources/", 10) == 0 || memcmp(p, "resources/", 10) == 0);
    }

    bool __cdecl hkFileExists(const char* path)
    {
        if (!path || !IsReadOnlyResource(path))
            return oFileExists(path);

        AcquireSRWLockShared(&g_lock);
        auto it = g_cache.find(path);
        bool found = (it != g_cache.end());
        bool cached = found ? it->second : false;
        ReleaseSRWLockShared(&g_lock);
        if (found)
        {
            if ((InterlockedIncrement(&g_served) & 0x1FFF) == 0)
                DumpStats();
            return cached;
        }

        bool ret = oFileExists(path);
        InterlockedIncrement(&g_disk);
        AcquireSRWLockExclusive(&g_lock);
        g_cache[path] = ret;
        ReleaseSRWLockExclusive(&g_lock);
        return ret;
    }
}

void InstallFileExistsCache()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oFileExists, hkFileExists);
    DetourTransactionCommit();
}
