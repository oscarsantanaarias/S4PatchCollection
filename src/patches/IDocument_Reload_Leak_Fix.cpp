#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    // FUN_01cb0300 (document setter) assigns the sibling field this+0xd8 through
    // the safe IntrusivePtr_Assign, but stores a freshly-built IDocument into
    // this+0xdc with a RAW store, never freeing the previous occupant. On the
    // reload path (FUN_01cb0a10) this+0xdc already holds a live IDocument, so
    // each reload leaks the old one. +0xdc is a uniquely-owned pointer: the class
    // destructor (FUN_01caf550) null-checks it and frees it via vtable[0](1).
    // Fix: free the old +0xdc the same way before the original overwrites it.
    const uintptr_t SET_DOC  = 0x01CB0300;
    const unsigned  DOC_OFF  = 0xDC;

    typedef void(__thiscall* tDelDtor)(void*, int);

    typedef void(__fastcall* tSetDoc)(void*, void*, int*);
    tSetDoc oSetDoc = nullptr;

    void __fastcall hkSetDoc(void* thisptr, void* edx, int* param_1)
    {
        void* old = *(void**)((char*)thisptr + DOC_OFF);
        if (old)
            ((tDelDtor)(*(void***)old)[0])(old, 1);
        oSetDoc(thisptr, edx, param_1);
    }
}

void InstallIDocumentReloadLeakFix()
{
    // no reinstalar: el puntero al original ya apunta al trampolin de Detours,
    // reasignarlo lo devolveria a la funcion parcheada y quedaria recursion infinita
    static bool installed = false;
    if (installed) return;
    installed = true;

    oSetDoc = (tSetDoc)S4(SET_DOC);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oSetDoc, hkSetDoc);
    DetourTransactionCommit();
}
