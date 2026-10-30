// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for core/bbsc_role.h. Plain C++20, no emulator headers:
//   g++ -std=c++20 -I emulator/src emulator/mod/tests/bbsc_role_test.cpp -o role_test && ./role_test
//   clang-cl /std:c++20 /EHsc /I emulator\src emulator\mod\tests\bbsc_role_test.cpp

#include <cstdio>

#include "core/bbsc_role.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

using Bbsc::Role::DepartureSendsHome;
using Bbsc::Role::OwnRoleKeeper;
constexpr int SummonPhase = 3;
constexpr int OtherPhase = 4;

// The game's departure check (eboot 0x01316640), as decompiled: a departure only sends the local
// player home when no leave is running, the leaver is not a slot marked as leaving, and the
// leaver's role has no "stays" mark in the role table.
void DepartureCheckModel() {
    Check(DepartureSendsHome(false, true, false, 0), "a leaver with role mark 0 sends us home");
    Check(!DepartureSendsHome(false, true, false, 1), "a leaver whose role is marked stays");
    Check(!DepartureSendsHome(false, true, true, 0), "a slot marked as leaving does not");
    Check(DepartureSendsHome(false, false, true, 0), "a leaver not in any slot does (mark unread)");
    Check(!DepartureSendsHome(true, true, false, 0), "nothing while a leave already runs (4047)");
}

// 2026-10-29 20:00-20:15: the guests joined with the game's own summon load, then BBSC's follow
// warp (phase 3, request role 0) left each guest's own role at 0, the host's role. Every later
// departure then sent the remaining players home.
void FollowKeepsTheSummonRole() {
    OwnRoleKeeper keeper;
    Check(keeper.OnLoadStep(SummonPhase, false, 7) == 0,
          "the game's own summon load is left alone");
    keeper.OnTick(true);
    Check(keeper.OnLoadStep(SummonPhase, true, 0) == 7,
          "BBSC's follow load in the session gets the summon role back");
    Check(keeper.OnLoadStep(SummonPhase, true, 0) == 7, "and every later BBSC reload too");
}

void LeavesOtherLoadsAlone() {
    OwnRoleKeeper keeper;
    keeper.OnLoadStep(SummonPhase, false, 7);
    keeper.OnTick(true);
    Check(keeper.OnLoadStep(OtherPhase, true, 0) == 0, "a non-summon load keeps the game's role 0");
    Check(keeper.OnLoadStep(SummonPhase, false, 0) == 0, "outside a session nothing is written");
    Check(keeper.OnLoadStep(SummonPhase, true, 9) == 0, "a load that has a role keeps its own");
    Check(keeper.OnLoadStep(SummonPhase, true, 0) == 9, "and that role is the one kept from then");
}

void ForgetsTheRoleWhenTheSessionEnds() {
    OwnRoleKeeper keeper;
    keeper.OnLoadStep(SummonPhase, false, 7);
    keeper.OnTick(false); // the summon load itself runs before the session state reads "guest"
    keeper.OnTick(true);
    keeper.OnTick(false); // back home
    keeper.OnTick(true);  // a later session that joined without a role of its own
    Check(keeper.OnLoadStep(SummonPhase, true, 0) == 0, "an old session's role is never reused");
}

void NothingKnownNothingWritten() {
    OwnRoleKeeper keeper;
    keeper.OnTick(true);
    Check(keeper.OnLoadStep(SummonPhase, true, 0) == 0, "no summon role seen, nothing written");
}

} // namespace

int main() {
    DepartureCheckModel();
    FollowKeepsTheSummonRole();
    LeavesOtherLoadsAlone();
    ForgetsTheRoleWhenTheSessionEnds();
    NothingKnownNothingWritten();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
