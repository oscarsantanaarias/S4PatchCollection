#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t DECRYPT_X7   = 0x01B42C50; // parse driver (this func)
    const uintptr_t FILE_LOAD    = 0x01B79480; // loader -> file object (EAX)
    const uintptr_t FILE_RELEASE = 0x01B79FB0; // release(mgr, obj) __thiscall
    const uintptr_t GET_MGR      = 0x01C7EE50; // -> file manager
    // Return addr del CALL a FUN_01b42c50 dentro de FUN_01d11310 (CALL E8 en
    // 0x01d113e4 -> retorna a 0x01d113e9). Gate para el fix B (leak solo de ese caller).
    const uintptr_t D11310_RET   = 0x01D113E9;
    const void*     D11310_SITE  = nullptr;

    typedef char(__fastcall* tDecrypt)(void*, void*, char*, char*, size_t, int);
    typedef void*(__fastcall* tFileLoad)(void*, void*, char*, char, char);
    typedef void(__fastcall* tFileRelease)(void*, void*, void*);
    typedef void*(__cdecl* tGetMgr)(void);

    tDecrypt     oDecrypt     = nullptr;
    tFileLoad    oFileLoad    = nullptr;
    tFileRelease oFileRelease = nullptr;
    tGetMgr      oGetMgr      = nullptr;

    // Fix B: el file object que FUN_01d11310 carga (FUN_01b79480) se libera SOLO en
    // el success path; si el parse falla, se leakea. Capturamos el ultimo file object
    // por-hilo y, cuando el parse falla siendo llamado desde FUN_01d11310, lo
    // liberamos igual que el success path (FUN_01b79fb0(mgr, obj)).
    // ponytail: solo se dispara con un .x7 corrupto (parse-fail) -> impacto nulo en
    // cliente normal; es robustez. TLS + gate por return-addr = sin efecto en otros callers.
    __declspec(thread) void* g_lastFileObj = nullptr;

    void* __fastcall hkFileLoad(void* thisptr, void* edx, char* p1, char p2, char p3)
    {
        void* obj = oFileLoad(thisptr, edx, p1, p2, p3);
        g_lastFileObj = obj;
        return obj;
    }

    char __fastcall hkDecrypt(void* thisptr, void* edx, char* Str, char* Src, size_t Size, int a5)
    {
        const void* ra = _ReturnAddress();
        char r;
        if (Size < 8)
        {
            r = 0;
        }
        else
        {
            __try { r = oDecrypt(thisptr, edx, Str, Src, Size, a5); }
            __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
            {
                r = 0;
            }
        }

        if (ra == D11310_SITE)
        {
            if (r == 0 && g_lastFileObj)
                oFileRelease(oGetMgr(), nullptr, g_lastFileObj);   // mgr en ECX, obj en stack
            g_lastFileObj = nullptr;   // consumir siempre (evita re-release stale)
        }
        return r;
    }
}

void InstallX7DecryptGuard()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oDecrypt     = (tDecrypt)S4(DECRYPT_X7);
    oFileLoad    = (tFileLoad)S4(FILE_LOAD);
    oFileRelease = (tFileRelease)S4(FILE_RELEASE);
    oGetMgr      = (tGetMgr)S4(GET_MGR);
    D11310_SITE  = (const void*)S4(D11310_RET);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oDecrypt, hkDecrypt);
    DetourAttach(&(PVOID&)oFileLoad, hkFileLoad);
    DetourTransactionCommit();
}
