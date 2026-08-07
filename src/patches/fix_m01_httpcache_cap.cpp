#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

namespace
{
    // CHTTPImageCache never evicts: GetImage (0x0142C570) -> LoadTexture
    // (0x0142C650) creates a texture and inserts it into a url-keyed std::map
    // via CHTTPImageCache_MapInsertIfAbsent (0x0142CA80). That insert helper is
    // GENERIC (many callers), so we can't hook the function. Instead we repoint
    // ONLY the call site inside LoadTexture (0x0142C776) to a guard that stops
    // inserting past a cap. The map is bounded; beyond the cap a distinct url
    // just re-creates its texture per request (owned by the caller, freed with
    // it) - no eviction of in-use textures, no null returns, no STL clear.
    const uintptr_t INSERT_FN    = 0x0142CA80;
    const uintptr_t CALL_SITE    = 0x0142C776; // call MapInsertIfAbsent inside LoadTexture
    const long      CACHE_CAP    = 1024;       // ponytail: max distinct cached server images; upgrade path = clear-on-channel-change if a safe clear routine is ever found

    // __thiscall: this in ecx, 4 stack args, callee-cleaned -> matches __fastcall with a dummy edx
    typedef void* (__fastcall* tInsert)(void* thisMap, void* edx, void* out, int a2, void* a3, void* a4);
    tInsert oInsert = (tInsert)INSERT_FN;
    volatile long g_count = 0;

    void* __fastcall guardInsert(void* thisMap, void* edx, void* out, int a2, void* a3, void* a4)
    {
        if (g_count < CACHE_CAP)
        {
            InterlockedIncrement(&g_count);
            return oInsert(thisMap, edx, out, a2, a3, a4);
        }
        *(int*)out = 0;          // cap reached: skip insert, return a defined empty pair (LoadTexture doesn't read it)
        *((int*)out + 1) = 0;
        return out;
    }

    bool RepointCall(uintptr_t site, void* target)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        DWORD old;
        if (!VirtualProtect((void*)(site + 1), 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *(int32_t*)(site + 1) = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect((void*)(site + 1), 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallFix_M01_HttpCacheCap()
{
    RepointCall(CALL_SITE, (void*)guardInsert);
}
