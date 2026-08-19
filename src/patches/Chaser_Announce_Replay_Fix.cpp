#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

// The briefing announces the chaser again. S2C_Briefing_21010 (0x00ACB0A0) calls
// Slaughter_AnnounceChaser (0x00A18CD0) before the isResult branch and only checks whether
// the client already has a chaser set, so every briefing that lands mid match replays the
// banner, the music and the sound cut as if the round were starting. The server can't
// avoid it: the briefing is the only packet that fills the scoreboard rows (level, clothes)
// for an intruder who joins mid round, and that same packet fires the announcement.
//
// The third argument is isReplay, and it is 1 only when the caller is the briefing. For the
// length of that call the four functions that make noise are stomped with their own ret,
// and put back on the way out:
//
//   0x007B95C0  Hud_ShowNotice(this, id)    retn 4    -> C2 04 00
//   0x00596330  play bgm                    retn 18h  -> C2 18 00
//   0x005970F0  stop sound by name          retn 8    -> C2 08 00
//   0x00596FB0  stop sound                  retn      -> C3
//
// The calls are stomped rather than skipped because the rest of the function still has to
// run: it is the one that rebuilds the chaser marker, and without it whoever joins mid round
// ends up with no marker at all. The client drives all of this from a single thread, which is
// the only thing that makes a stomp this blunt safe.
namespace
{
    const uintptr_t ANNOUNCE_FN = 0x00A18CD0;

    typedef int(__fastcall* tAnnounce)(void* self, void* edx, unsigned idLow, unsigned idHigh, char isReplay);
    tAnnounce oAnnounce = nullptr;

    struct Gag
    {
        uintptr_t address;
        BYTE stomp[3];
        BYTE size;
        BYTE saved[3];
    };

    Gag g_gags[] = {
        { 0x007B95C0, { 0xC2, 0x04, 0x00 }, 3, { 0 } }, // Hud_ShowNotice
        { 0x00596330, { 0xC2, 0x18, 0x00 }, 3, { 0 } }, // bgm
        { 0x005970F0, { 0xC2, 0x08, 0x00 }, 3, { 0 } }, // stop sound by name
        { 0x00596FB0, { 0xC3, 0x00, 0x00 }, 1, { 0 } }, // stop sound
    };

    void Write(uintptr_t at, const void* bytes, size_t size)
    {
        DWORD old;
        if (!VirtualProtect((LPVOID)at, size, PAGE_EXECUTE_READWRITE, &old))
            return;
        memcpy((void*)at, bytes, size);
        VirtualProtect((LPVOID)at, size, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)at, size);
    }

    void Gags(bool on)
    {
        for (size_t i = 0; i < sizeof(g_gags) / sizeof(g_gags[0]); ++i)
        {
            if (on)
            {
                memcpy(g_gags[i].saved, (const void*)g_gags[i].address, g_gags[i].size);
                Write(g_gags[i].address, g_gags[i].stomp, g_gags[i].size);
            }
            else
            {
                Write(g_gags[i].address, g_gags[i].saved, g_gags[i].size);
            }
        }
    }

    int __fastcall hkAnnounce(void* self, void* edx, unsigned idLow, unsigned idHigh, char isReplay)
    {
        if (!isReplay)
            return oAnnounce(self, edx, idLow, idHigh, isReplay);

        Gags(true);
        int result = oAnnounce(self, edx, idLow, idHigh, isReplay);
        Gags(false);
        return result;
    }
}

void InstallChaserAnnounceReplayFix()
{
    // no reinstall: applying it twice would break the hook
    static bool installed = false;
    if (installed) return;
    installed = true;

    oAnnounce = (tAnnounce)ANNOUNCE_FN;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)oAnnounce, hkAnnounce);
    DetourTransactionCommit();
}
