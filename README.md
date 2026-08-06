# S4Fixes

Client-side hardening for the S10 client, S4Client.exe, image base 0x00400000.
Each fix is a Detours hook, added one at a time as it gets confirmed.

## Layout

- `s4fixes.sln` / `src/s4fixes.vcxproj` - the project, Win32 only.
- `src/dllmain.cpp` - loads every patch on `DLL_PROCESS_ATTACH`.
- `src/patches/` - one file per bug fix. Everything in here is a patch,
  nothing else goes in this folder.
- `src/documentacion/` - why each fix exists, how the bug was confirmed.
- `third_party/` - Detours, the hooking library.

## Build

Open `s4fixes.sln`, build `Release|Win32`. `OutDir` is `C:\S4Plain\` -
change it in the vcxproj if your client lives elsewhere.

## Fixes

- `patches/fix_c03_arcade_overflow.cpp` - stack buffer overflow in the
  arcade P2P entry reader. A crafted entry from another player can overflow
  a char[256] stack buffer. Confirmed firing in Warfare, primitive isn't
  arcade-only despite the name.
