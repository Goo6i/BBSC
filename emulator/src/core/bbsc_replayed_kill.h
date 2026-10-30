// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC: no kill reward for an enemy the host had already killed when it dies again in a guest's
// load of the host's Chalice Dungeon. Plain logic, no game memory, so it is tested on its own
// (mod/tests/bbsc_replayed_kill_test.cpp).
//
// A guest's load builds the dungeon's enemies alive; the host's game then tells the guest's game
// which ones are dead, and they die there. The kill reward 0x018C5070 runs for each death the
// guest's game sees and pays it unless chr+0x1E1 & 0x240000 (its gate at 0x018C50AF; 0x40000 =
// paid). Outside the Chalice Dungeons the game marks these deaths 0x200000 and pays nothing
// (test-4 2026-10-30 r7im, Cathedral Ward 14:58:01 and 15:02:59). In a Chalice Dungeon they come
// unmarked and are paid, again at every load and at every BBSC reinsert (which restarts the host's
// player insert): 2026-10-30 r7im +167237 echoes 0.0-0.1 s after a reinsert with the host killing
// nothing; 2026-10-29 Ali the same seven enemies paid at 20:08:48 and again at 20:15:23, 5.4-7.3 s
// after a reinsert; BBSC's guest item drops are rolled for them too. The guest's real shares of the
// host's kills that day (each in the host's own log) came 14.2 s or more after a load or reinsert.
// So a guest's death in a Chalice Dungeon within WindowMs of either gets the game's own 0x200000
// before the reward reads the flags: no echoes, no BBSC drop roll, as in every other area.

#pragma once

#include <cstdint>

namespace Bbsc::ReplayedKill {

inline constexpr std::uint32_t NoRewardBits = 0x240000; // the reward's own gate (0x018C50AF)
inline constexpr std::uint32_t ReplayedBit = 0x200000;  // the game's mark on replayed deaths
inline constexpr std::int64_t WindowMs = 10'000;
inline constexpr int GuestPlayerType = 1;   // a summoned cooperator
inline constexpr int InHostsWorldState = 6; // session state: in a host's world

enum class Verdict {
    Off,             // SHADPS4_BBSC_CHALICE_REPLAYED_KILLS=0: as BBSC 0.0.2
    AlreadyNoReward, // the game marked it (0x200000) or paid it (0x40000) itself
    NotAGuest,       // the host's own kills, or not in a host's world
    NotInChalice,    // other areas: the game marks replayed deaths itself
    NoLoadYet,       // no load in the host's world to measure from
    AfterWindow,     // a kill: paid
    Replayed,        // the host's earlier kill dying again in our load: not paid
};

struct Death {
    bool enabled = true;
    int player_type = 0;
    int session_state = 0;
    std::uint32_t map = 0;   // the current packed map (block in the top byte)
    std::uint32_t flags = 0; // dword at the dead character +0x1E1
    std::int64_t now_ms = 0;
    std::int64_t load_ms = -1; // our last load in the host's world or BBSC reinsert; -1 none
};

constexpr bool IsChaliceMap(std::uint32_t packed_map) {
    return (packed_map & 0xFF000000u) == 0x1D000000u; // m29_xx
}

constexpr Verdict Judge(const Death& d) {
    if (!d.enabled) {
        return Verdict::Off;
    }
    if ((d.flags & NoRewardBits) != 0) {
        return Verdict::AlreadyNoReward;
    }
    if (d.player_type != GuestPlayerType || d.session_state != InHostsWorldState) {
        return Verdict::NotAGuest;
    }
    if (!IsChaliceMap(d.map)) {
        return Verdict::NotInChalice;
    }
    if (d.load_ms < 0) {
        return Verdict::NoLoadYet;
    }
    // A load stamped a moment after the death (another thread's clock read) is the same load.
    return d.now_ms - d.load_ms <= WindowMs ? Verdict::Replayed : Verdict::AfterWindow;
}

constexpr bool ShouldMark(Verdict v) {
    return v == Verdict::Replayed;
}

constexpr std::uint32_t Mark(std::uint32_t flags) {
    return flags | ReplayedBit;
}

constexpr const char* Describe(Verdict v) {
    switch (v) {
    case Verdict::Off:
        return "switch off (SHADPS4_BBSC_CHALICE_REPLAYED_KILLS=0): paid as in 0.0.2";
    case Verdict::AlreadyNoReward:
        return "the game marked it itself";
    case Verdict::NotAGuest:
        return "not a guest in a host's world";
    case Verdict::NotInChalice:
        return "not in a Chalice Dungeon";
    case Verdict::NoLoadYet:
        return "no load in the host's world yet";
    case Verdict::AfterWindow:
        return "a kill (after the load's replay window)";
    case Verdict::Replayed:
        return "the host's earlier kill, replayed into our load - no reward";
    }
    return "?";
}

} // namespace Bbsc::ReplayedKill
