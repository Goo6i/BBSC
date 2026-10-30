// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for core/bbsc_host_placement.h. Plain C++20, no emulator headers:
//   g++ -std=c++20 -I emulator/src emulator/mod/tests/bbsc_host_placement_test.cpp -o hp_test && ./hp_test
//   clang-cl /std:c++20 /EHsc /I emulator\src emulator\mod\tests\bbsc_host_placement_test.cpp

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include "core/bbsc_host_placement.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

struct Spot {
    std::uint32_t packed_region{};
    float x{};
};

bool Same(const std::optional<Spot>& a, const Spot& b) {
    return a.has_value() && a->packed_region == b.packed_region && a->x == b.x;
}

using Keeper = Bbsc::HostPlacement::ClaimPlacement<Spot>;
constexpr std::uint32_t Dream = 0x15000000;
constexpr std::uint32_t Chalice = 0x1d000000;

// 2026-10-29 19:55-19:59, host in the Hunter's Dream: the first ring carried our placement and the
// guest joined 4 s later; every later ring (after a summon build) carried none for 4 minutes, and
// the second guest's claims twice never became a join.
void TheSecondRingStillCarriesOurPlacement() {
    Keeper k;
    k.OnOwnRead(Spot{Dream, 1.0f}, "captured");
    k.OnSummonBuild(Spot{Dream, 2.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Dream, 2.0f}), "first ring: our placement at the build");
    k.OnSummonBuild(Spot{Dream, 3.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Dream, 3.0f}), "second ring after a same-map build: still ours");
    k.OnSummonBuild(Spot{Dream, 4.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Dream, 4.0f}), "third ring: the position at that build");
}

void BuildReadIsFresherThanTheTickRead() {
    Keeper k;
    k.OnOwnRead(Spot{Dream, 1.0f}, "captured");
    k.OnSummonBuild(Spot{Dream, 9.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Dream, 9.0f}), "the claim uses the build's read, not the older tick's");
}

void NoPositionAtTheBuildMeansNone() {
    Keeper k;
    k.OnOwnRead(Spot{Dream, 1.0f}, "captured");
    k.OnSummonBuild(std::nullopt, "invalid_transform");
    Check(!k.ForClaim().has_value(), "build could not read our position: no placement sent");
    Check(k.WhyNone() == "summon build: invalid_transform", "and the log says why");
}

void TheNextGoodReadBringsItBack() {
    Keeper k;
    k.OnSummonBuild(std::nullopt, "invalid_map");
    k.OnOwnRead(Spot{Chalice, 5.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Chalice, 5.0f}), "a later good read gives the next ring a placement");
    Check(k.WhyNone().empty(), "and clears the reason");
}

void AFailedTickReadKeepsTheLastGoodOne() {
    // RefreshSeamlessLocalPlacement (SOS status tick) ignores a failed read, as before.
    Keeper k;
    k.OnOwnRead(Spot{Dream, 1.0f}, "captured");
    k.OnOwnRead(std::nullopt, "invalid_manager");
    Check(Same(k.ForClaim(), Spot{Dream, 1.0f}), "failed tick read: the last good placement stays");
}

void NothingReadYet() {
    Keeper k;
    Check(!k.ForClaim().has_value(), "fresh start: no placement");
    Check(k.WhyNone() == "never read", "fresh start: reason");
}

void AGoodBuildClearsAnOldReason() {
    Keeper k;
    k.OnSummonBuild(std::nullopt, "invalid_transform");
    k.OnSummonBuild(Spot{Dream, 2.0f}, "captured");
    Check(Same(k.ForClaim(), Spot{Dream, 2.0f}) && k.WhyNone().empty(),
          "a good build after a failed one: placement, no reason");
}

} // namespace

int main() {
    TheSecondRingStillCarriesOurPlacement();
    BuildReadIsFresherThanTheTickRead();
    NoPositionAtTheBuildMeansNone();
    TheNextGoodReadBringsItBack();
    AFailedTickReadKeepsTheLastGoodOne();
    NothingReadYet();
    AGoodBuildClearsAnOldReason();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
