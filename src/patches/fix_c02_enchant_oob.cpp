#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <cstring>
#include <cstdint>

namespace
{
    const uintptr_t LOAD_ENCHANT       = 0x01C207C0;
    const uintptr_t LOAD_ENCHANT_LEVEL = 0x01C25600;
    const uintptr_t MEMCPY_SITE_1      = 0x01C20B2D;
    const uintptr_t MEMCPY_SITE_2      = 0x01C25945;
    const unsigned  STRIDE_1           = 24;
    const unsigned  STRIDE_2           = 16;
    const unsigned  FALLBACK_ROWS      = 4096; // ponytail: window used only if heap size lookup fails; upgrade path = read the true row-count field if ever identified

    struct Table { uintptr_t base; SIZE_T size; };
    Table g_t1 = { 0, 0 };
    Table g_t2 = { 0, 0 };
    volatile LONG g_blocked = 0;

    SIZE_T BlockSize(void* p)
    {
        HANDLE heaps[64];
        DWORD got = GetProcessHeaps(64, heaps);
        for (DWORD i = 0; i < got; i++)
        {
            if (HeapValidate(heaps[i], 0, p))
            {
                SIZE_T s = HeapSize(heaps[i], 0, p);
                if (s != (SIZE_T)-1)
                    return s;
            }
        }
        return 0;
    }

    void CaptureBase(Table& t, uintptr_t base, unsigned stride)
    {
        t.base = base;
        SIZE_T s = BlockSize((void*)base);
        t.size = s ? s : (SIZE_T)stride * FALLBACK_ROWS;
    }

    bool InRange(const Table& t, uintptr_t dst, unsigned stride)
    {
        if (!t.base)
            return true; // loader not seen yet (shouldn't happen) -> don't break loading
        return dst >= t.base && (dst + stride) <= (t.base + t.size);
    }

    typedef char(__fastcall* tLoad)(void*, void*, void*);
    tLoad oLoad1 = (tLoad)LOAD_ENCHANT;
    tLoad oLoad2 = (tLoad)LOAD_ENCHANT_LEVEL;

    char __fastcall hkLoad1(void* thisptr, void* edx, void* a2)
    {
        CaptureBase(g_t1, (uintptr_t)thisptr, STRIDE_1);
        return oLoad1(thisptr, edx, a2);
    }
    char __fastcall hkLoad2(void* thisptr, void* edx, void* a2)
    {
        CaptureBase(g_t2, (uintptr_t)thisptr, STRIDE_2);
        return oLoad2(thisptr, edx, a2);
    }

    void* __cdecl guard1(void* dst, const void* src, size_t n)
    {
        if (!InRange(g_t1, (uintptr_t)dst, STRIDE_1))
        {
            InterlockedIncrement(&g_blocked);
            return dst;
        }
        return memcpy(dst, src, n);
    }
    void* __cdecl guard2(void* dst, const void* src, size_t n)
    {
        if (!InRange(g_t2, (uintptr_t)dst, STRIDE_2))
        {
            InterlockedIncrement(&g_blocked);
            return dst;
        }
        return memcpy(dst, src, n);
    }

    bool RepointCall(uintptr_t site, void* target)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        uintptr_t* rel = (uintptr_t*)(site + 1);
        DWORD old;
        if (!VirtualProtect(rel, 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *(int32_t*)rel = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect(rel, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallFix_C02_EnchantOOB()
{
    RepointCall(MEMCPY_SITE_1, (void*)guard1);
    RepointCall(MEMCPY_SITE_2, (void*)guard2);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oLoad1, hkLoad1);
    DetourAttach(&(PVOID&)oLoad2, hkLoad2);
    DetourTransactionCommit();
}
