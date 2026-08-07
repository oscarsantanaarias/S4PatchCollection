#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    // Decrypt_X7_File @ 0x01B42C50 decrypts an encrypted .x7. Two file-controlled
    // crash vectors (T3 swappable file):
    //   Sizea = (Size - 8) >> 2;  operator new[](Sizea) x4;  loop reads 4*Sizea
    //     bytes from Src  -> a file < 8 bytes underflows Size-8 to ~0x3FFFFFFF ->
    //     ~1GB x4 allocs + massive OOB read of Src.
    //   v26 = *Src ^ 0xFE292513;  operator new[](v26 + 1)  -> the declared
    //     decompressed size is the first 4 file bytes XOR a key, so an attacker
    //     sets it to ~4GB -> OOM throw.
    // Fix: gate Size >= 8 (with 8+ bytes, Sizea = (Size-8)>>2 and the loop reads
    // 4*Sizea = Size-8 bytes from Src+8 -> stays inside the buffer, OOB read gone)
    // and SEH-wrap the call so the ~4GB alloc throw fail-softs to 0 instead of
    // killing the client.
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t DECRYPT_X7 = 0x01B42C50;

    typedef char(__fastcall* tDecrypt)(void*, void*, char*, char*, size_t, int);
    tDecrypt oDecrypt = (tDecrypt)DECRYPT_X7;

    char __fastcall hkDecrypt(void* thisptr, void* edx, char* Str, char* Src, size_t Size, int a5)
    {
        if (Size < 8)
            return 0; // malformed/hostile short file -> drop before the underflow
        __try
        {
            return oDecrypt(thisptr, edx, Str, Src, Size, a5);
        }
        __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            return 0; // ~4GB decompressed-size alloc threw -> fail-soft
        }
    }
}

void InstallFix_S2_X7Decrypt()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oDecrypt, hkDecrypt);
    DetourTransactionCommit();
}
