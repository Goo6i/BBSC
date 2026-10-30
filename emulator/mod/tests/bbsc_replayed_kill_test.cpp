// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later
// Tests for core/bbsc_replayed_kill.h. Build and run (no game needed):
//   clang-cl /std:c++20 /EHsc /W4 /WX /I emulator\src emulator\mod\tests\bbsc_replayed_kill_test.cpp
//   g++ -std=c++20 -Wall -Wextra -I emulator/src emulator/mod/tests/bbsc_replayed_kill_test.cpp
//
// The deaths are the ones the players' logs traced on 2026-10-29 and 2026-10-30 ("BBSC echo
// path: kill reward entry ..."), measured from the last time the guest's load in the host's world
// finished or BBSC's reinsert restarted the host's player insert.

#include <cstdint>
#include <cstdio>

#include "core/bbsc_replayed_kill.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

using namespace Bbsc::ReplayedKill;

constexpr int Guest = 1;
constexpr int Host = 0;
constexpr int InHostsWorld = 6;
constexpr std::uint32_t ChaliceMap = 0x1D000000;   // "now in Chalice Dungeon (m29_00)"
constexpr std::uint32_t CathedralMap = 0x18000000; // test-4 r7im 14:58:01, an ordinary map

// A guest in the host's Chalice Dungeon whose load (or BBSC's reinsert) was `after_ms` ago.
Death GuestDeath(std::int64_t after_ms, std::uint32_t flags = 0) {
    Death d;
    d.player_type = Guest;
    d.session_state = InHostsWorld;
    d.map = ChaliceMap;
    d.flags = flags;
    d.now_ms = 1'000'000;
    d.load_ms = d.now_ms - after_ms;
    return d;
}

} // namespace

int main() {
    // 2026-10-30 13:07:37.333-.417, r7im on the host's dungeon 6s46u7i4 (channel 9233574): seven
    // deaths 0 to 100 ms after "reinsert v8: restarted 1 remote player insert task(s)", flags 0,
    // paid 49112 + 6 x 17154 echoes. The host killed nothing then.
    Check(Judge(GuestDeath(0)) == Verdict::Replayed, "10-30 r7im: a death at the reinsert itself");
    Check(Judge(GuestDeath(97)) == Verdict::Replayed, "10-30 r7im: 97 ms after the reinsert");

    // 2026-10-29 Ali: the same seven enemies (5845 5845 5914 4662 4662 13346 5427) died 5.4 to
    // 7.3 s after a reinsert (20:08:48-50), and again 5.7 to 7.2 s after the next (20:15:23-24).
    Check(Judge(GuestDeath(5'405)) == Verdict::Replayed, "10-29 Ali: 5.4 s after the reinsert");
    Check(Judge(GuestDeath(7'295)) == Verdict::Replayed, "10-29 Ali: 7.3 s after the reinsert");

    // 2026-10-29 r7im: deaths 0.4 to 0.6 s after the summon's load finished (20:12:48), before any
    // reinsert.
    Check(Judge(GuestDeath(402)) == Verdict::Replayed, "10-29 r7im: 0.4 s after the load");

    // Ali's real shares of the host's kills (each one in the host's log within 3 s): the nearest
    // came 14.2 s after a reinsert (20:01:56, 5427); the others 40 s and more.
    Check(Judge(GuestDeath(14'200)) == Verdict::AfterWindow, "10-29 Ali: real kill 14.2 s after");
    Check(Judge(GuestDeath(41'500)) == Verdict::AfterWindow, "10-29 Ali: real kill 41.5 s after");
    Check(Judge(GuestDeath(14'200)) != Verdict::Replayed, "a real kill is paid");

    // The window's edge.
    Check(Judge(GuestDeath(WindowMs)) == Verdict::Replayed, "exactly the window: replayed");
    Check(Judge(GuestDeath(WindowMs + 1)) == Verdict::AfterWindow, "1 ms past it: paid");
    Check(WindowMs == 10'000, "the window is 10 s (replays seen up to 7.3 s, kills from 14.2 s)");
    // Our own clock read on another thread can put the load a moment after the death.
    Check(Judge(GuestDeath(-30)) == Verdict::Replayed, "a load stamped 30 ms after the death");

    // The game's own no-reward marks stay the game's: 0x200000 on deaths a load replays outside
    // the Chalice Dungeons (test-4 r7im 14:58:01, 3 deaths, nothing paid), 0x40000 once paid.
    Check(Judge(GuestDeath(500, 0x200000)) == Verdict::AlreadyNoReward, "marked 0x200000 already");
    Check(Judge(GuestDeath(500, 0x40000)) == Verdict::AlreadyNoReward, "paid already (0x40000)");
    Check(Judge(GuestDeath(500, 0x1000)) == Verdict::Replayed, "other bits do not count");

    // Only a guest in the host's world, only in the Chalice Dungeons.
    Death host = GuestDeath(500);
    host.player_type = Host;
    Check(Judge(host) == Verdict::NotAGuest, "the host's kills (10-29 host log: all real)");
    Death joining = GuestDeath(500);
    joining.session_state = 4;
    Check(Judge(joining) == Verdict::NotAGuest, "not yet in the host's world (joining)");
    Death solo = GuestDeath(500);
    solo.session_state = 0;
    solo.player_type = Host;
    Check(Judge(solo) == Verdict::NotAGuest, "alone in our own world");
    Death cathedral = GuestDeath(500);
    cathedral.map = CathedralMap;
    Check(Judge(cathedral) == Verdict::NotInChalice, "an ordinary map: the game marks those");
    Check(IsChaliceMap(0x1D000000) && IsChaliceMap(0x1D32003B), "Chalice maps (block 0x1D)");
    Check(!IsChaliceMap(0x15000000) && !IsChaliceMap(0x18000000) && !IsChaliceMap(0),
          "Hunter's Dream, Cathedral Ward, none");
    Check(!IsChaliceMap(0x1C000000) && !IsChaliceMap(0x1E000000), "the blocks either side");

    // Nothing to measure from yet: paid as before.
    Death fresh = GuestDeath(500);
    fresh.load_ms = -1;
    Check(Judge(fresh) == Verdict::NoLoadYet, "no load in the host's world yet");

    // Switch off (SHADPS4_BBSC_CHALICE_REPLAYED_KILLS=0): BBSC 0.0.2, everything paid.
    Death off = GuestDeath(0);
    off.enabled = false;
    Check(Judge(off) == Verdict::Off, "switch off: as 0.0.2");

    // Only Replayed changes anything; every verdict says what it is.
    Check(Describe(Verdict::Replayed)[0] != '\0' && Describe(Verdict::AfterWindow)[0] != '\0' &&
              Describe(Verdict::Off)[0] != '\0' && Describe(Verdict::NotInChalice)[0] != '\0' &&
              Describe(Verdict::NotAGuest)[0] != '\0' && Describe(Verdict::NoLoadYet)[0] != '\0' &&
              Describe(Verdict::AlreadyNoReward)[0] != '\0',
          "every verdict has a description");
    Check(ShouldMark(Verdict::Replayed) && !ShouldMark(Verdict::AfterWindow) &&
              !ShouldMark(Verdict::Off) && !ShouldMark(Verdict::AlreadyNoReward) &&
              !ShouldMark(Verdict::NotInChalice) && !ShouldMark(Verdict::NotAGuest) &&
              !ShouldMark(Verdict::NoLoadYet),
          "only a replayed death gets the game's no-reward mark");
    Check(Mark(0) == ReplayedBit && Mark(0x1000) == (0x1000u | ReplayedBit) &&
              (Mark(0) & NoRewardBits) != 0,
          "the mark is the game's own 0x200000, which its gate tests");

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
