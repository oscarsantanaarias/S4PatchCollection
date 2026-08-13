#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../s4_base.h"
#include "detours.h"
#include <string>
#include <unordered_map>

namespace
{
    // El bool mas llamado del pipeline de recursos: pega a disco en cada consulta,
    // decenas de miles por carga. Los recursos de solo-lectura no cambian en runtime,
    // asi que la respuesta se cachea.
    typedef bool(__cdecl* tFileExists)(const char*);
    const uintptr_t FILE_EXISTS = 0x010385E0;
    tFileExists oFileExists = nullptr;

    std::unordered_map<std::string, bool> g_cache;
    SRWLOCK g_lock = SRWLOCK_INIT;

    volatile LONG g_served = 0;
    volatile LONG g_disk   = 0;

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
            InterlockedIncrement(&g_served);
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

// Contadores por export, para medir la mejora sin escribir nada a disco.
extern "C" __declspec(dllexport) long FileExistsServed() { return g_served; }
extern "C" __declspec(dllexport) long FileExistsDisk()   { return g_disk; }

void InstallFileExistsCache()
{
    oFileExists = (tFileExists)S4(FILE_EXISTS);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oFileExists, hkFileExists);
    DetourTransactionCommit();
}
