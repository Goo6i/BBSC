// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC: a guest's own multiplayer role across BBSC's reloads. Plain logic, no game memory, so it
// is tested on its own (mod/tests/bbsc_role_test.cpp).
//
// Every player's game keeps its own role at [[0x0553B130]+8]+0x5D8 and sends it to the others
// (player data sync 0x01783160 writes it into the sender's slot, [0x0553B130]+0x10 + slot*0x6A0,
// +0x5D8). The in-game step that every map load builds (0x01937570) sets that role: on a summon
// load (summon phase [[0x0553D6D0]+0x84] == 3) to byte +0x88 of the load request it is given,
// on any other load to 0, the host's role.
//
// When a member's character is deleted, the game's departure check (0x01316640) decides whether
// the departure means "my host is gone": unless a leave already runs (event flag 4047) or the
// leaver's slot carries the leaving mark ([0x0553B130]+0xA9+slot*2, set when a player dies), it
// looks the leaver's role up in the role table (0x0553D750, 0x22 roles of 0x80 bytes, byte +0x15)
// and, when that byte is 0, schedules OnLeave_Limit in 5 s: the trip home.
//
// BBSC's own reloads in a session (follow warp, rejoin) set summon phase 3 but never fill the
// request's role, so each guest came out of a follow with role 0. Its next departure then sent
// every remaining player home (2026-10-29: the host 5.0 s after each guest left, BBSC skips that
// on a host; the other guest 5.2 s after, reloaded at the lamp). OwnRoleKeeper keeps the role
// the game's own summon load used and gives it to BBSC's later summon-phase loads.

#pragma once

#include <cstdint>

namespace Bbsc::Role {

constexpr int SummonLoadPhase = 3;

// The game's departure check, for logging what it decides.
constexpr bool DepartureSendsHome(bool leave_running, bool leaver_found,
                                  bool leaver_marked_leaving, std::uint8_t role_mark) {
    if (leave_running) {
        return false;
    }
    if (leaver_found && leaver_marked_leaving) {
        return false;
    }
    return role_mark == 0;
}

// Switch SHADPS4_BBSC_KEEP_ROLE (on by default). The role BBSC writes into a reload request given
// the role OnLoadStep returned: off writes nothing, so the request stays as BBSC 0.0.2 leaves it.
constexpr std::uint8_t RoleToWrite(bool keep_role_switch, std::uint8_t kept) {
    return keep_role_switch ? kept : 0;
}

class OwnRoleKeeper {
public:
    // At the start of the in-game step of a load. Returns the role to put into the load request
    // (byte +0x88) before the step reads it, or 0 to leave the request alone.
    std::uint8_t OnLoadStep(int summon_phase, bool guest_in_session, std::uint8_t request_role) {
        if (summon_phase != SummonLoadPhase) {
            return 0;
        }
        if (request_role != 0) {
            kept_ = request_role; // the game's own summon (or a load that already has a role)
            return 0;
        }
        return guest_in_session ? kept_ : 0;
    }

    // Every tick: whether the local player is a guest in a session. The role is kept from the
    // summon load until the session that followed it ends.
    void OnTick(bool guest_in_session) {
        if (guest_in_session) {
            in_session_ = true;
        } else if (in_session_) {
            in_session_ = false;
            kept_ = 0;
        }
    }

    std::uint8_t Kept() const {
        return kept_;
    }

private:
    std::uint8_t kept_ = 0;
    bool in_session_ = false;
};

} // namespace Bbsc::Role
