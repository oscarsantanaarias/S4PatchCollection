# S4Fixes

Client-side fixes and enhancements for S4 League, injected as a DLL. Each fix is
a Detours hook or an in-place patch, added one at a time as it gets confirmed
against the disassembly.

## Layout

- `s4fixes.sln` / `src/s4fixes.vcxproj` - the project, Win32 only.
- `src/dllmain.cpp` - installs everything from a thread off `DLL_PROCESS_ATTACH`.
- `src/s4_base.h` - the `S4()` macro, see below.
- `src/patches/` - one file per bug fix, nothing else goes in this folder.
- `src/enhancements/` - features rather than fixes.
- `src/overlay/` - the in-game settings menu, with a vendored ImGui.
- `src/Console.cpp` - the diagnostics console.
- `third_party/` - Detours.

## Addresses

`S4(va)` fixes each address up against the real module base at load time, so one
build keeps working if the exe is ever relocated or repacked.

Every address was relocated against this binary and checked before being used.
The files used to carry addresses from a different build, which land inside this
image too, so instead of failing they hooked unrelated code and none of the
guards were doing anything. Where a fix is missing an address, it stays at zero
and is skipped rather than hooking blind.

Installers are idempotent. Running one twice used to be worse than a no-op: the
call repointing takes whatever the site points at as the original, so a second
pass would take our own hook and the hook would end up calling itself.

## Build

Open `s4fixes.sln`, build `Release|Win32`. Output goes to `bin/`. To have it land
in your client folder too, set `S4ClientDir` in a `s4fixes.vcxproj.user`, which
stays out of the repo. The copy is skipped, not failed, when the file is in use.

## Overlay and console

`INSERT` toggles an in-game settings menu, off by default. Max framerate and field
of view, both applied live. FOV is one value with a tunable sprint offset, since
the game derives its three view states from a single number. Settings persist to
`conf.json`, written with defaults on first run.

It's drawn by hooking the device vtable, `EndScene` for the frame and `Reset` for
resolution changes. The subclassed `WndProc` only forwards input while the menu is
open, so the game keeps keyboard and mouse the rest of the time.

The console prints the resource lookup and fail-soft counters on one line that
rewrites itself. Set `ENABLED` to false in `Console.cpp` to drop it. Nothing here
writes to disk.

## Fixes

- `Arcade_StackOverflow_Fix.cpp` - the stream read primitive calls
  `memcpy_s(dst, size, src, size)`, the same size as both the bound and the
  count, so it never protects the destination and the caller passes a fixed
  buffer. Its own check only covers the source. Reachable from the peer path,
  which also reads its counts through this primitive.
- `LzoS4_Size_Guard.cpp` - every LZO caller that does `new[](header + 1)`: a
  `0xFFFFFFFF` header wraps to zero and the raw header is then handed to the
  decompressor as the output capacity. The list comes from scanning the image for
  calls to the decompressor rather than trusting an existing one, which is how the
  9 vulnerable call sites turned up.
- `X7Decrypt_Guard.cpp` - encrypted `.x7` decrypt underflows `(size - 8) >> 2` on
  a file under 8 bytes and allocates the size from the first four bytes, up to
  ~4GB. It also leaks the loaded file object when the parse fails, since only the
  success path frees it.
- `P2P_Container_DoS_Guard.cpp` - the two peer-controlled count reads driving the
  20025 container walk. A peer sets the count to `0x7FFFFFFF` and every iteration
  allocates and retains a handler, so the client either runs out of memory or
  freezes for minutes. A real sub-entry costs at least 4 bytes, so the count is
  clamped to what's left in the stream.
- `Arcade_Alloc_Fix.cpp` - the arcade stream does `operator new[]` on a 4-byte
  size from the peer, up to a ~2GB request. Clamped to 4MB.
- `StructVector_Cap_Fix.cpp` - the struct-vector reader trusts a ~1M element
  count, which is an iteration and allocation DoS. Repoints that reader's cap
  compare to a smaller per-reader cap, leaving the shared one alone.
- `BlobTree_Guard.cpp` - a two stage deserializer where the first worker returns a
  size read from the source and the second starts at that offset without checking
  it still lands inside the buffer, so a peer gets an over-read.
- `XmlOverRead_Guard.cpp` - the XML parser advances the cursor by the delimiter
  length after a failed search for a comment or CDATA terminator, so it runs off
  the end of the buffer. The return is clamped to the first NUL instead.
- `S4hd_Mount_Guard.cpp` - the mount routine subtracts `0x20` from the size with
  no lower bound, so a file under `0x20` bytes wraps the unsigned and asks for an
  enormous allocation, which throws where nothing catches it.
- `Memory_Jump_Fix.cpp` - a negative value takes a branch that stores it into the
  struct and the caller then uses it as a size. The conditional jump that normally
  skips that branch is forced, one byte, and only when the expected byte is still
  there.
- `IDocument_Reload_Leak_Fix.cpp` - the document setter assigns one field through
  the intrusive pointer helper but stores the new document into its sibling with a
  raw store, so every reload leaks the previous one. That field is uniquely owned:
  the destructor null-checks it and frees it through the vtable.
- `ResWrapper_Leak_Fix.cpp` - when the resource factory returns null the branch
  that frees the wrapper is the other one, so every failed load leaks it.
- `HttpImageCache_Fix.cpp` - the HTTP texture cache never evicts, so the map grows
  for as long as the session lasts and feeds the long-session freeze. Capped at
  1024 entries.
- `Scene_Leak_Fix.cpp` - the anti-reload half only: a 500ms window per slot,
  against the reload spam that shows up as Not Responding. Interning is off here,
  because this loader normalizes its key before the lookup, so its keys already
  match and interning would add a reference on objects the game frees. Known side
  effect of the window: it occasionally skips a legitimate reload and leaves a
  collectionbook item on a stale scene, a red block. Lower `ANTIRELOAD_MS` or turn
  it off if it bothers you.
- `LoadingTip_Cache.cpp` - the loading tip table is cleared and reparsed off disk
  on every loading screen. Loads once instead.
- `FileExists_Cache.cpp` - the most called check in the resource pipeline opens
  and closes a file handle on every query, and read-only resources don't change
  while the game runs. It was hitting the disk over two hundred thousand times per
  session; now under eight thousand. The counters are exported and shown in the
  overlay.
- `Heap_Fixes.cpp` - the Low Fragmentation Heap for every heap in the process,
  including the ones created later, plus fail-soft wrappers on the allocators so a
  `bad_alloc` returns null instead of killing the process. 15 of the 19 wrappers
  are located; the rest stay at zero and are skipped.
- `Enchant_OOB_Fix.cpp` - nothing to patch here. The renewal enchant loaders and
  their anchor strings don't exist in this build, and the file stays so the reason
  is recorded.

## Enhancements

- `Fps.cpp` - unlocks the framerate and keeps the frame-dependent physics behaving
  the same at any rate: jump height, flight, exo jump, the plasma sword stun drop
  and weapon spread. Also drives the FOV override the overlay edits, applied at
  the camera's target field and restored afterwards, because leaving our value in
  place stops the state machine from ever writing the sprint value again.
- `New_Actor_State.cpp` - actor state additions.
