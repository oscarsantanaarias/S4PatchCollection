# S4Fixes

Client-side hardening for the S10 client, S4Client.exe, image base 0x00400000.
Each fix is a Detours hook, added one at a time as it gets confirmed.

## Layout

- `s4fixes.sln` / `src/s4fixes.vcxproj` - the project, Win32 only.
- `src/dllmain.cpp` - loads every patch on `DLL_PROCESS_ATTACH`.
- `src/patches/` - one file per bug fix. Everything in here is a patch,
  nothing else goes in this folder.
- `third_party/` - Detours, the hooking library.

## Build

Open `s4fixes.sln`, build `Release|Win32`. `OutDir` is `C:\S4Plain\` -
change it in the vcxproj if your client lives elsewhere.

## Fixes

- `patches/fix_c03_arcade_overflow.cpp` - stack buffer overflow in the
  arcade P2P entry reader. A crafted entry from another player can overflow
  a char[256] stack buffer. Confirmed firing in Warfare, primitive isn't
  arcade-only despite the name.
- `patches/heap_fixes.cpp` - C-01: unbounded reliable-frame count handed to
  the frame-map resize (0x1B0DAF0), 8*count wraps 32-bit to a tiny alloc ->
  heap overflow. Rejects counts over 0x100000. Also the OOM fail-soft hooks.
- `patches/fix_c02_enchant_oob.cpp` - C-02: renewal-enchant .x7 loaders
  (0x1C207C0 / 0x1C25600) write each row at a file-controlled INDEX with no
  bound. Guards both memcpy sites against the table's real allocated range.
- `patches/fix_m01_httpcache_cap.cpp` - M-01: CHTTPImageCache (0x142C650)
  never evicts -> unbounded texture map feeds the long-session freeze. Repoints
  the insert call site inside LoadTexture (0x142C776) to cap the map at 1024.
- `patches/fix_l01_structvec_cap.cpp` - L-01: struct-vector reader (0x1918580)
  trusts a ~1M element count -> iteration/alloc DoS. Repoints that reader's cap
  compare (0x19185DA) to a smaller per-reader cap, leaving the shared cap alone.
- `patches/fix_dos_arcade_alloc.cpp` - arcade P2P stream alloc (0x1155B90) does
  operator new[] on an attacker 4-byte size -> ~2GB OOM. Clamps to 4MB.
- `patches/fix_perf_g1_tipreload.cpp` - perf: loading-tip table re-parsed every
  loading screen (0x13DA240). Repoints its reparse call (0x13DA462) to parse once.
