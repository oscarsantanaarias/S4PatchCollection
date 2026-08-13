#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    // Deserializer de dos etapas: el primer worker devuelve un tamano leido del propio
    // origen y el segundo arranca en param_1 + ese tamano sin validar que siga dentro
    // del buffer, asi que un peer consigue un over-read.
    const uintptr_t BLOB_ENTRY = 0x01124230;

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
    oEntry = (tEntry)S4(BLOB_ENTRY);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oEntry, hkEntry);
    DetourTransactionCommit();
}
