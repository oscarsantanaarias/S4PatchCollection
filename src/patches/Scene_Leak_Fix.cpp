#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>
#include <string>
#include <cstring>
#include <cstdint>
#include "../s4_base.h"

namespace
{
    // Habia una cache que internaba imagenes por ruta: se saco. El loader normaliza la
    // clave en el sitio y busca e inserta con esa misma, o sea la cache interna del juego
    // si acierta y no habia leak por-carga. Ademas devolver temprano se salteaba el
    // registro del recurso en la escena, y el AddRef por vtbl[1] era una suposicion sobre
    // un puntero que el loader devuelve PRESTADO.
    //
    // Queda el anti-reload contra el spam de recargas. Pero no puede saltear a ciegas:
    // lo PRIMERO que hace SetScene es guardar la ruta en el string del slot
    // (this + 0xbc + slot*0x18). Si no se llama, ese campo queda sin escribir y quien lo
    // lea despues se lleva un puntero nulo. De ahi salia tambien el item de collectionbook
    // que quedaba en una scene vieja. Ahora saltea solo si el slot YA tiene esa ruta.

    const DWORD ANTIRELOAD_MS = 500;

    const uintptr_t SET_SCENE_ADDR = 0x01177E90;
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
