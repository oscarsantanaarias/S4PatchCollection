#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    // XML parser over-read (A1/A2): FUN_01b47500 (comment "-->") y FUN_01b45780
    // (CDATA "]]>" / atributo con comilla) hacen `cursor += strlen(delim)` tras un
    // loop que sale en el NUL del buffer O al matchear el delim, SIN chequear cuál.
    // En input sin cerrar (comment/CDATA/atributo truncado) el loop sale en el NUL,
    // y el += avanza el puntero retornado 1-3 bytes MÁS ALLÁ del buffer. El over-READ
    // real lo hace el caller al derefear ese puntero.
    // Fix: no tocar el parser. Detour ambas funcs y CLAMPear el puntero de retorno
    // al primer NUL alcanzable desde el cursor de entrada -> si el retorno se pasó
    // del NUL, el caller recibe el NUL y para ahí. Input válido: el retorno queda
    // antes del NUL (hay más contenido) -> sin cambios.

    const uintptr_t COMMENT = 0x00B8F1C0; // __thiscall(this, cursor, out, endTag)
    const uintptr_t DELIM   = 0x00B8D7C0; // __cdecl(cursor, delim?, ...) -> cursor

    // Clampa `ret` para que no supere el primer NUL alcanzable desde `from`.
    // Solo lee bytes válidos (se detiene en el NUL, que está dentro del buffer).
    static unsigned char* Clamp(unsigned char* from, unsigned char* ret)
    {
        if (!ret || !from) return ret;
        unsigned char* p = from;
        while (*p && p < ret) p++;
        return (p < ret) ? p : ret;   // NUL antes de ret -> clamp; si no, sin cambio
    }

    typedef unsigned char*(__fastcall* tComment)(void*, void*, unsigned char*, void*, int);
    typedef unsigned char*(__cdecl* tDelim)(unsigned char*, const char*, char, char*, char, int);

    tComment oComment = (tComment)COMMENT;
    tDelim   oDelim   = (tDelim)DELIM;

    unsigned char* __fastcall hkComment(void* thisptr, void* edx, unsigned char* cursor, void* out, int endTag)
    {
        return Clamp(cursor, oComment(thisptr, edx, cursor, out, endTag));
    }

    unsigned char* __cdecl hkDelim(unsigned char* cursor, const char* p2, char p3, char* p4, char p5, int p6)
    {
        return Clamp(cursor, oDelim(cursor, p2, p3, p4, p5, p6));
    }
}

void InstallXmlOverReadGuard()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oComment, hkComment);
    DetourAttach(&(PVOID&)oDelim, hkDelim);
    DetourTransactionCommit();
}
