#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

namespace
{
    // Los dos decoders del contenedor encriptado (key 0xFE292513). Aca el bug es el
    // tamano sin cota inferior:
    //
    //   uVar1 = param_3 - 8 >> 2;              <- si param_3 < 8 underflowea
    //   new[](uVar1) x4                        <- cuatro allocs de ~1 GB
    //
    // Con un archivo de menos de 8 bytes eso termina en un bad_alloc que nadie atrapa.
    // El otro tamano del mismo decoder (el ^key con el new(size+1) que wrappea) ya lo
    // acota el clamp del LZO, que engancha ese mismo new-site.
    const DWORD CPP_EXC = 0xE06D7363;
    const uintptr_t DECODER_A = 0x00B88550;
    const uintptr_t DECODER_B = 0x00B8B800;
    const unsigned  MIN_SIZE  = 8;

    typedef void(__fastcall* tDecoder)(void*, void*, char*, void*, unsigned, unsigned);
    tDecoder oDecoderA = nullptr;
    tDecoder oDecoderB = nullptr;

    void __fastcall hkDecoderA(void* thisptr, void* edx, char* p1, void* p2, unsigned size, unsigned p4)
    {
        if (size < MIN_SIZE)
            return;
        __try { oDecoderA(thisptr, edx, p1, p2, size, p4); }
        __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    }

    void __fastcall hkDecoderB(void* thisptr, void* edx, char* p1, void* p2, unsigned size, unsigned p4)
    {
        if (size < MIN_SIZE)
            return;
        __try { oDecoderB(thisptr, edx, p1, p2, size, p4); }
        __except (GetExceptionCode() == CPP_EXC ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    }
}

void InstallX7DecryptGuard()
{
    oDecoderA = (tDecoder)DECODER_A;
    oDecoderB = (tDecoder)DECODER_B;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oDecoderA, hkDecoderA);
    DetourAttach(&(PVOID&)oDecoderB, hkDecoderB);
    DetourTransactionCommit();
}
