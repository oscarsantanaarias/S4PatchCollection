#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"
#include "../s4_base.h"

namespace
{
    // El setter asigna el hermano this+0xd8 con IntrusivePtr_Assign pero mete el
    // IDocument nuevo en this+0xdc con un store CRUDO, sin liberar al anterior: en el
    // reload +0xdc ya tiene uno vivo, asi que se filtra uno por recarga. +0xdc es de
    // propiedad unica (el destructor lo null-checkea y lo libera por vtable[0](1)), asi
    // que liberamos igual antes de pisarlo.
    const uintptr_t SET_DOC  = 0x0115AD90;
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
    oSetDoc = (tSetDoc)S4(SET_DOC);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oSetDoc, hkSetDoc);
    DetourTransactionCommit();
}
