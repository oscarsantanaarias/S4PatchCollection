# S4Fixes

Client-side fixes and enhancements for S4 League, injected as a DLL. Each fix is
a Detours hook or an in-place patch, added one at a time as it gets confirmed
against the disassembly.

## Layout

- `s4fixes.sln` / `src/s4fixes.vcxproj` - the project, Win32 only.
- `src/dllmain.cpp` - installs everything from a thread off `DLL_PROCESS_ATTACH`.
- `src/patches/` - one file per bug fix, nothing else goes in this folder.
- `src/enhancements/` - features rather than fixes.
- `src/overlay/` - the in-game panel, with a vendored ImGui.
- `third_party/` - Detours.

## Addresses

The client is unpacked and loads at its own image base, so addresses are written
plain. Every one of them was checked against the binary before being used: the
files used to carry addresses from a different build, which land inside this
image too, so instead of failing they hooked unrelated code and none of the
guards were doing anything.

Installers are idempotent. Running one twice used to be worse than a no-op: the
call repointing takes whatever the site points at as the original, so a second
pass would take our own hook and the hook would end up calling itself.

## Build

Open `s4fixes.sln`, build `Release|Win32`. Output goes to `bin/`. To have it land
in your client folder too, set `S4ClientDir` in a `s4fixes.vcxproj.user`, which
stays out of the repo. The copy is skipped, not failed, when the file is in use.

## Overlay

`INSERT` toggles the panel, off by default. Framerate cap, field of view and the
physics rate, all applied live, plus the current actor state. Settings persist to
`conf.json`, written with defaults on first run, and saved again when the panel
closes so the hotkeys make it in too. It's drawn by hooking the device vtable, `EndScene` for the frame and `Reset` for resolution
changes. The subclassed `WndProc` only forwards input while the panel is open, so
the game keeps keyboard and mouse the rest of the time.

## Fixes

- `LzoS4_Size_Guard.cpp` - every LZO caller that does `new[](header + 1)`: a
  `0xFFFFFFFF` header wraps to zero and the raw header is then handed to the
  decompressor as the output capacity, so it writes past the allocation. The list
  comes from the decompressor's own references, 8 callers, the 6 that pair a raw
  header with that allocation. The other two are a thin wrapper and one that
  allocates exactly the capacity it passes.
- `X7Decrypt_Guard.cpp` - the two encrypted-container decoders compute
  `(size - 8) >> 2` with no lower bound, so a file under 8 bytes underflows it and
  asks for four allocations near a gigabyte, ending in a `bad_alloc` nobody
  catches. Rejects anything shorter than the header.
- `Manifest_Read_Clamp.cpp` - the manifest parsers read a 4-byte length off the
  stream and copy that many bytes into a 276-byte stack buffer, through a reader
  that never learns the destination size. Clamped at the reader, which is the
  choke point both callers go through.
- `Lzo_Unbounded_Guard.cpp` - the raw LZO1X path has no output bound and the P2P
  dispatcher decompresses into a fixed 2KB global, which is the one that actually
  corrupts memory from the wire.
- `Arcade_Alloc_Fix.cpp` - the arcade stream allocator takes a 4-byte size from
  the peer and sets its end pointer to base plus that, with no cap. Clamped to
  4MB.
- `XmlOverRead_Guard.cpp` - the XML parser advances the cursor by the delimiter
  length after a failed search for a comment or CDATA terminator, so it runs off
  the end of the buffer. The return is clamped to the first NUL instead.
- `Memory_Jump_Fix.cpp` - a negative value takes a branch that stores it into the
  struct and returns success, and the caller then uses it as a size. The
  conditional jump that normally skips that branch is forced, one byte, and only
  when the expected byte is still there.
- `ResWrapper_Leak_Fix.cpp` - when the resource factory can't match the extension
  it returns null, and the branch that frees the wrapper is the other one, so
  every failed load leaks it.
- `HttpImageCache_Fix.cpp` - the HTTP texture cache is a map by url with no
  eviction, so it grows for as long as the session lasts. Capped at 1024 entries.
  The insert is hooked directly rather than one call site, since all three of its
  references belong to that same map.
- `FileExists_Cache.cpp` - the most called check in the resource pipeline opens
  and closes a file handle on every query, tens of thousands per load, and
  read-only resources don't change while the game runs. The counters are exported
  so the difference is measurable.
- `Heap_Fixes.cpp` - turns on the Low Fragmentation Heap for every heap in the
  process, including the ones created later. Fragmentation was ending sessions
  with a freeze while half the address space was still free.

Some fixes aren't here because the bug isn't: no renewal enchant tables, no
recursive size-driven deserializer, no count-driven peer container, no document
reload path, and a mount routine that allocates the full size instead of
subtracting from it. The scene loader normalizes its key before the lookup, so
its keys already match and interning would only add a reference the game doesn't
expect.

## Enhancements

`Fps.cpp` - the engine's time source has about 15ms of resolution, which turns
into a coarse delta once the frame rate goes up, and that is what made movement
depend on it. It's replaced with a high resolution counter, aligned to the
original epoch so nothing jumps, and quantized to the physics rate so the game
always sees the same step. The cap is a tight spin gated by the game's own
toggle, saved and restored instead of left off.

FOV is taken at the projection matrix, the last point before the frame is drawn.
The camera field it used to write was only a cache and another writer kept
overwriting it, which is why the value drifted back. It's applied as an offset
with ramps at the band edges, so the sprint bonus and the sniper scope survive
and there's no hard jump crossing between them.

Jump, wing flight and the fly evade use flat multipliers so height doesn't change
with the frame rate, and the plasma stun drop is held to one spike frame. `PgUp`
and `PgDn` move the FOV, `F5` lifts the cap.
