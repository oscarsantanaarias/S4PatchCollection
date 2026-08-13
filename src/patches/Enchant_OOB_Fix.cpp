#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Este build no trae el renewal enchant: no existen los strings ancla
// (RENEWAL_ENCHANT_PRICE / ITEM_LEVEL / INDEX) ni los dos loaders stride-24/16 que hacen
// el memcpy a index*stride + this sin validar el index. No hay nada que parchear.
void InstallEnchantOOBFix()
{
    // no reinstalar: aplicarlo dos veces romperia el hook
    static bool installed = false;
    if (installed) return;
    installed = true;

}
