#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    const uintptr_t BLOB_ENTRY = 0x01C79000;

    typedef char(__fastcall* tEntry)(void*, void*, void*);
    tEntry oEntry = (tEntry)BLOB_ENTRY;

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
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oEntry, hkEntry);
    DetourTransactionCommit();
}
