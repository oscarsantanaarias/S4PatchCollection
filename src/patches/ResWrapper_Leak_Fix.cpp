#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include <cstdint>
#include "../s4_base.h"

namespace
{
    // load-or-adopt del ResMgr: si el factory (el call en +0xD7) devuelve NULL, el if de
    // abajo no entra y nadie libera el wrapper que llego por param_2, asi que se filtra
    // uno por cada load fallido. Solo la rama factory!=NULL libera.
    const uintptr_t LOAD_OR_ADOPT = 0x00FF34D0;
    const uintptr_t FACTORY_SITE  = 0x00FF35A7;

    thread_local void* g_wrapper = nullptr;

    void Release(void* obj)
    {
        if (!obj) return;
        void** vt = *(void***)obj;
        typedef void(__fastcall* tRel)(void*, void*, int);
        ((tRel)vt[0])(obj, nullptr, 1);
    }

    typedef void*(__fastcall* tLoadOrAdopt)(void*, void*, char*, void*);
    tLoadOrAdopt oLoadOrAdopt = nullptr;

    void* __fastcall hkLoadOrAdopt(void* thisptr, void* edx, char* path, void* wrapper)
    {
        void* prev = g_wrapper;
        g_wrapper = wrapper;
        void* r = oLoadOrAdopt(thisptr, edx, path, wrapper);
        g_wrapper = prev;
        return r;
    }

    typedef int(__fastcall* tFactory)(void*, void*, void*);
    tFactory oFactory = nullptr;

    int __fastcall hkFactory(void* mgr, void* edx, void* buf)
    {
        int piVar3 = oFactory(mgr, edx, buf);
        if (piVar3 == 0 && g_wrapper)
        {
            Release(g_wrapper);
            g_wrapper = nullptr;
        }
        return piVar3;
    }

    bool RepointCall(uintptr_t site, void* target, void** origOut)
    {
        if (*(uint8_t*)site != 0xE8)
            return false;
        int32_t* rel = (int32_t*)(site + 1);
        *origOut = (void*)(site + 5 + *rel);
        DWORD old;
        if (!VirtualProtect(rel, 4, PAGE_EXECUTE_READWRITE, &old))
            return false;
        *rel = (int32_t)((uintptr_t)target - (site + 5));
        VirtualProtect(rel, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)site, 5);
        return true;
    }
}

void InstallResWrapperLeakFix()
{
    oLoadOrAdopt = (tLoadOrAdopt)S4(LOAD_OR_ADOPT);

    RepointCall(S4(FACTORY_SITE), (void*)hkFactory, (void**)&oFactory);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oLoadOrAdopt, hkLoadOrAdopt);
    DetourTransactionCommit();
}
