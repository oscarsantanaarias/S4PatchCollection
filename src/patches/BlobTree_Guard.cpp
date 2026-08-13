#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    const uintptr_t BLOB_ENTRY = 0x01C79000;

    typedef char(__fastcall* tEntry)(void*, void*, void*);
    tEntry oEntry = nullptr;

    char __fastcall hkEntry(void* thisptr, void* edx, void* src)
    {
        __try
        {
            return oEntry(thisptr, edx, src);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            return 0;
        }
    }
}

void InstallBlobTreeGuard()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oEntry = (tEntry)S4(BLOB_ENTRY);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oEntry, hkEntry);
    DetourTransactionCommit();
}
