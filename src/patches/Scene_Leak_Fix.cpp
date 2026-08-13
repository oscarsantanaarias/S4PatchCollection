#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include "../s4_base.h"
#include <string>
#include <cstring>
#include <cstdint>

namespace
{
    // Este parche tenia dos mitades y las dos estaban mal:
    //
    // - Una cache externa que internaba imagenes por ruta, partiendo de que la cache
    //   interna del juego nunca acierta porque busca normalizado e inserta crudo. No es
    //   asi: FUN_01cb8500 normaliza la cadena en el sitio y busca e inserta con esa
    //   misma. Ademas devolver temprano se salteaba el registro del recurso en la
    //   escena. Se saco entera.
    //
    // - Un anti-reload que si el slot ya tenia esa ruta hace menos de 500ms no llamaba
    //   al original. Eso crasheaba: lo PRIMERO que hace SetScene es guardar la ruta en
    //   el string del slot (this + 0xbc + slot*0x18); si no se llama, ese string queda
    //   sin escribir, y FUN_01ccce90 despues le pide el .c_str() y se lo pasa a una
    //   virtual -> lectura de NULL adentro del CRT.
    //
    // Queda el anti-reload, pero salteando solo cuando el juego YA tiene esa ruta
    // guardada en el slot: ahi el estado esta puesto y no se pierde nada.

    const DWORD ANTIRELOAD_MS = 500;

    const uintptr_t SET_SCENE_ADDR = 0x01CCCF50;
    const unsigned  SLOT_PATH_OFF = 0xbc;   // std::string por slot, stride 0x18
    const unsigned  SLOT_STRIDE = 0x18;

    std::string Normalize(const char* path)
    {
        std::string s(path ? path : "");
        for (char& c : s)
        {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        }
        return s;
    }

    // std::string de MSVC: buffer o puntero en +0, tamano en +0x10, capacidad en +0x14.
    // Devuelve false si no parece un string valido, y ahi no se saltea nada.
    bool LeerCrudo(void* obj, unsigned slot, const char** texto, unsigned* largo)
    {
        unsigned char* p = (unsigned char*)obj + SLOT_PATH_OFF + slot * SLOT_STRIDE;
        __try
        {
            unsigned size = *(unsigned*)(p + 0x10);
            unsigned res  = *(unsigned*)(p + 0x14);
            if (res < 15 || size > res || size > 0x1000) return false;
            const char* s = (res < 16) ? (const char*)p : *(const char**)p;
            if (!s) return false;
            *texto = s;
            *largo = size;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RutaDelSlot(void* obj, unsigned slot, std::string& out)
    {
        const char* s = nullptr;
        unsigned n = 0;
        if (!LeerCrudo(obj, slot, &s, &n)) return false;
        out.assign(s, n);
        return true;
    }

    typedef void (__thiscall* tSetScene)(void* thisptr, unsigned slot, char* path);
    tSetScene oSetScene = nullptr;

    struct SlotState { std::string path; DWORD time; };
    SlotState g_slot[10];
    CRITICAL_SECTION g_cs;

    void __fastcall hkSetScene(void* thisptr, void* edx, unsigned slot, char* path)
    {
        if (path && slot < 10 && thisptr)
        {
            const std::string key = Normalize(path);
            const DWORD now = GetTickCount();

            std::string actual;
            const bool yaPuesta = RutaDelSlot(thisptr, slot, actual) && Normalize(actual.c_str()) == key;

            EnterCriticalSection(&g_cs);
            SlotState& s = g_slot[slot];
            const bool repetida = (s.path == key) && (now - s.time) < ANTIRELOAD_MS;
            s.path = key;
            s.time = now;
            LeaveCriticalSection(&g_cs);

            // solo se saltea si ademas el juego ya tiene el estado puesto
            if (repetida && yaPuesta)
                return;
        }
        oSetScene(thisptr, slot, path);
    }
}

void InstallSceneLeakFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    InitializeCriticalSection(&g_cs);
    oSetScene = (tSetScene)S4(SET_SCENE_ADDR);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oSetScene, hkSetScene);
    DetourTransactionCommit();
}
