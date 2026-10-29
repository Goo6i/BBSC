// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for core/bbsc_chalice.h. Plain C++20, no emulator headers:
//   g++ -std=c++20 -I emulator/src emulator/mod/tests/bbsc_chalice_test.cpp -o chalice_test
//   clang-cl /std:c++20 /EHsc /I emulator\src emulator\mod\tests\bbsc_chalice_test.cpp

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "core/bbsc_chalice.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

namespace C = Bbsc::Chalice;
using C::Descriptor;

constexpr std::uint32_t ChaliceMap = 0x1D000000; // the packed map of every Chalice Dungeon load
constexpr std::uint32_t DreamMap = 0x15000000;

// Real load requests from a solo probe run (2026-10-29 21:45, test build 1), bytes +0xA0..+0xFF.
// 6s46u7i4 (channel 9233574, dungeon map 0x1D32003B) entered on layer 3:
constexpr Descriptor Desc6s46Layer3{
    0xa6, 0xe4, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3b, 0x00, 0x32, 0x1d, 0x0e, 0x37, 0x97, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x13, 0x37, 0x97, 0x00, 0x23, 0x00, 0x00, 0x00, 0x22, 0x37, 0x97, 0x00,
    0x32, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x3c, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xa0, 0x86, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00};
// 462yygcg (channel 116979, dungeon map 0x1D0A005D) entered on layer 1, the load before:
constexpr Descriptor Desc462yLayer1{
    0xf3, 0xc8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5d, 0x00, 0x0a, 0x1d, 0x0e, 0x37, 0x97, 0x00,
    0x1e, 0x00, 0x00, 0x00, 0x13, 0x37, 0x97, 0x00, 0x23, 0x00, 0x00, 0x00, 0x22, 0x37, 0x97, 0x00,
    0x32, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xa0, 0x86, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1d, 0x00, 0x00, 0x00};

// The Holygrail entry of 6s46u7i4 in the same run (entry 1): the fields a descriptor is made of.
std::array<std::uint8_t, C::EntryBytes> Entry6s46() {
    std::array<std::uint8_t, C::EntryBytes> entry{};
    std::memcpy(entry.data() + C::EntryChannel, Desc6s46Layer3.data() + 0x00, 8);
    std::memcpy(entry.data() + C::EntryDungeonMap, Desc6s46Layer3.data() + 0x08, 4);
    std::memcpy(entry.data() + C::EntryPairs, Desc6s46Layer3.data() + 0x0C, 0x48);
    std::memcpy(entry.data() + C::EntryValue, Desc6s46Layer3.data() + 0x54, 4);
    entry[C::EntryChannel - 8] = 0x11; // an unrelated pointer byte before the record
    return entry;
}

Descriptor WithChannel(Descriptor d, std::uint64_t channel) {
    std::memcpy(d.data(), &channel, 8);
    return d;
}

// The in-game step picks Holygrail entry [7 - bit length] of flags 9020..9026 as the game's flag
// reader returns them (MSB first: the last flag, 9026, is bit 0). The probe run entered entries
// 3, 2 and 1 with flags 9023, 9022 and 9021 on.
void SlotRule() {
    for (std::uint32_t flag = 9020; flag <= 9026; ++flag) {
        const std::uint32_t game_value = 1u << (9026 - flag);
        if (C::SlotOf(game_value) != static_cast<int>(flag - 9020)) {
            Check(false, "flag 9020+k selects entry k");
            return;
        }
    }
    Check(true, "flag 9020+k selects entry k");
    Check(C::SlotFlag(0) == 9020 && C::SlotFlag(3) == 9023, "slot k is event flag 9020+k");
}

void DescriptorFields() {
    Check(C::Channel(Desc6s46Layer3) == 9233574, "descriptor channel (6s46u7i4 = 9233574)");
    Check(C::DungeonMap(Desc6s46Layer3) == 0x1D32003B, "descriptor dungeon map id");
    Check(C::Layer(Desc6s46Layer3) == 2 && C::Layer(Desc462yLayer1) == 0, "descriptor layer, 0-based");
    Check(C::Fetchable(Desc6s46Layer3), "an online dungeon can be fetched by its channel");
    Check(!C::Fetchable(WithChannel(Desc6s46Layer3, C::OfflineChannel)),
          "an offline dungeon (no channel) cannot");
    Check(!C::Fetchable(WithChannel(Desc6s46Layer3, 0)), "channel 0 cannot");
    Descriptor not_chalice = Desc6s46Layer3;
    not_chalice[0x0B] = 0x15;
    Check(!C::Fetchable(not_chalice), "a descriptor that is not a Chalice dungeon cannot");
    Descriptor set = Desc6s46Layer3;
    C::SetLayer(set, 0);
    Check(C::Layer(set) == 0 && C::Channel(set) == 9233574, "SetLayer changes only the layer");
}

// The self-test builds the request from entry 0 over whatever the altar left in the request: the
// result must be exactly what the game wrote itself when the player entered 6s46u7i4 layer 3.
void DescriptorFromEntry() {
    const auto entry = Entry6s46();
    const Descriptor built = C::FromEntry(entry.data(), Desc462yLayer1, 2);
    Check(built == Desc6s46Layer3, "entry + layer over a stale request = the game's own request");
    const Descriptor layer1 = C::FromEntry(entry.data(), Desc462yLayer1, 0);
    Check(C::Layer(layer1) == 0 && C::Channel(layer1) == 9233574, "the layer is the one asked for");
    // cummmfpk (ritual 8) left 800000 in the request: the value has to come from the entry.
    Descriptor after_cummmfpk = Desc462yLayer1;
    const std::uint32_t ritual8_value = 800000;
    std::memcpy(after_cummmfpk.data() + 0x54, &ritual8_value, 4);
    Check(C::FromEntry(entry.data(), after_cummmfpk, 2) == Desc6s46Layer3,
          "the per-chalice value comes from the entry, not the stale request");
}

void Packet() {
    const auto frame = C::EncodePacket(7, Desc6s46Layer3);
    Check(frame.size() == C::PacketSize, "packet size");
    Check(std::memcmp(frame.data(), "SHAD", 4) == 0 && frame[4] == C::PacketType,
          "packet starts with SHAD and its type");
    const auto back = C::DecodePacket(frame.data(), frame.size());
    Check(back.has_value() && back->sequence == 7 && back->descriptor == Desc6s46Layer3,
          "packet round trip");
    Check(!C::DecodePacket(frame.data(), frame.size() - 1).has_value(), "a short packet is dropped");
    auto bad = frame;
    bad[4] = 0x27;
    Check(!C::DecodePacket(bad.data(), bad.size()).has_value(), "another type is dropped");
    bad = frame;
    bad[0] = 'X';
    Check(!C::DecodePacket(bad.data(), bad.size()).has_value(), "no SHAD, dropped");
    bad = frame;
    bad[5] = 0;
    Check(!C::DecodePacket(bad.data(), bad.size()).has_value(), "version 0 is dropped");
    Descriptor dream = Desc6s46Layer3;
    dream[0x0B] = 0x15;
    const auto odd = C::EncodePacket(1, dream);
    Check(!C::DecodePacket(odd.data(), odd.size()).has_value(),
          "a descriptor that is no Chalice dungeon is dropped");
}

void HostSide() {
    C::HostKeeper host;
    Check(!host.ToSend(true, ChaliceMap).has_value(), "nothing known, nothing sent");
    host.OnLoad(ChaliceMap, false, Desc6s46Layer3); // entered from the altar, solo or hosting
    const auto sent = host.ToSend(true, ChaliceMap);
    Check(sent.has_value() && sent->descriptor == Desc6s46Layer3, "a host in its dungeon sends it");
    Check(!host.ToSend(false, ChaliceMap).has_value(), "not hosting: nothing sent");
    Check(!host.ToSend(true, DreamMap).has_value(), "not standing in the Chalice: nothing sent");
    const std::uint32_t first = sent.has_value() ? sent->sequence : 0;
    host.OnLoad(ChaliceMap, true, Desc462yLayer1); // our load as someone's guest
    const auto still = host.ToSend(true, ChaliceMap);
    Check(still.has_value() && still->descriptor == Desc6s46Layer3,
          "a load as a guest does not change our own dungeon");
    host.OnLoad(ChaliceMap, false, Desc462yLayer1); // the host entered another dungeon
    const auto next = host.ToSend(true, ChaliceMap);
    Check(next.has_value() && next->descriptor == Desc462yLayer1 && next->sequence != first,
          "a new Chalice load replaces it");
    host.OnLoad(DreamMap, false, Desc462yLayer1);
    Check(!host.ToSend(true, ChaliceMap).has_value(), "a load out of the Chalice forgets it");
}

C::PrepInput In(std::int64_t now, const Descriptor* host, C::FetchState fetch,
                std::uint64_t entry0_channel = 0, bool entry0_ready = false, bool online = true) {
    C::PrepInput in{};
    in.now_ms = now;
    if (host != nullptr) {
        in.host = *host;
    }
    in.fetch = fetch;
    in.entry0_channel = entry0_channel;
    in.entry0_ready = entry0_ready;
    in.network_ready = online;
    return in;
}

using C::Action;
using C::FetchState;
using C::Status;

// Vanilla's own guest path (018bb200): fetch the host's channel, wait for state 3, build entry 0.
void GuestPrepHappyPath() {
    C::GuestPrep prep;
    const Descriptor* host = &Desc6s46Layer3;
    Check(prep.Tick(In(0, nullptr, FetchState::None)) == Action::None && prep.status() == Status::Idle,
          "no host dungeon: idle");
    Check(prep.Tick(In(100, host, FetchState::None)) == Action::StartFetch &&
              prep.status() == Status::Working,
          "host in a dungeon: fetch its channel");
    Check(prep.Tick(In(350, host, FetchState::Requesting)) == Action::None,
          "while the server answers: wait");
    Check(prep.Tick(In(600, host, FetchState::Ready)) == Action::BuildEntry,
          "channel arrived: build entry 0");
    Check(prep.Tick(In(850, host, FetchState::Requesting, 9233574, true)) == Action::ReleaseFetch &&
              prep.status() == Status::Ready,
          "entry 0 holds the host's dungeon: ready, our fetch released");
    Check(prep.Tick(In(1100, host, FetchState::None, 9233574, true)) == Action::None &&
              prep.status() == Status::Ready,
          "stays ready");
    Check(prep.target() == 9233574, "target is the host's channel");
}

void GuestPrepAlreadyThere() {
    C::GuestPrep prep;
    Check(prep.Tick(In(0, &Desc6s46Layer3, FetchState::None, 9233574, true)) == Action::None &&
              prep.status() == Status::Ready,
          "entry 0 already holds it: ready without a fetch");
    C::GuestPrep other;
    Check(other.Tick(In(0, &Desc6s46Layer3, FetchState::None, 116979, true)) == Action::StartFetch,
          "entry 0 holds another dungeon: fetch");
    C::GuestPrep busy;
    Check(busy.Tick(In(0, &Desc6s46Layer3, FetchState::None, 9233574, false)) == Action::StartFetch,
          "the right channel but not built: fetch and build");
}

void GuestPrepOffline() {
    C::GuestPrep prep;
    const Descriptor offline = WithChannel(Desc6s46Layer3, C::OfflineChannel);
    Check(prep.Tick(In(0, &offline, FetchState::None)) == Action::None &&
              prep.status() == Status::Failed,
          "the host's dungeon has no channel: failed, nothing fetched");
    C::GuestPrep net;
    Check(net.Tick(In(0, &Desc6s46Layer3, FetchState::None, 0, false, false)) == Action::None &&
              net.status() == Status::Working,
          "the game is not online yet: no fetch, still working");
}

void GuestPrepRetries() {
    C::GuestPrep prep;
    const Descriptor* host = &Desc6s46Layer3;
    std::int64_t now = 0;
    int fetches = 0;
    for (int round = 0; round < 10 && prep.status() != Status::Failed; ++round) {
        const Action a = prep.Tick(In(now, host, FetchState::None));
        if (a == Action::StartFetch) {
            ++fetches;
            now += 300;
            Check(prep.Tick(In(now, host, FetchState::Failed)) == Action::ReleaseFetch,
                  "a failed fetch is released");
        }
        now += C::RetryDelayMs / 2;
    }
    Check(fetches == C::MaxFetchAttempts && prep.status() == Status::Failed,
          "the server never sends it: failed after the last attempt");

    C::GuestPrep slow;
    Check(slow.Tick(In(0, host, FetchState::None)) == Action::StartFetch, "fetch started");
    Check(slow.Tick(In(C::FetchTimeoutMs - 1, host, FetchState::Requesting)) == Action::None,
          "still within the time limit");
    Check(slow.Tick(In(C::FetchTimeoutMs + 1, host, FetchState::Requesting)) == Action::ReleaseFetch,
          "no answer in time: released");
    Check(slow.Tick(In(C::FetchTimeoutMs + 2, host, FetchState::None)) == Action::None,
          "and retried only after the delay");
    Check(slow.Tick(In(C::FetchTimeoutMs + 2 + C::RetryDelayMs, host, FetchState::None)) ==
              Action::StartFetch,
          "retried after the delay");

    C::GuestPrep build;
    Check(build.Tick(In(0, host, FetchState::None)) == Action::StartFetch, "fetch started");
    int builds = 0;
    for (int i = 0; i < 10 && build.status() != Status::Failed; ++i) {
        if (build.Tick(In(100 + i, host, FetchState::Ready)) == Action::BuildEntry) {
            ++builds;
        }
    }
    Check(builds == C::MaxBuildAttempts && build.status() == Status::Failed,
          "entry 0 never becomes ready: failed after the last build");
}

void GuestPrepHostMoves() {
    C::GuestPrep inside;
    inside.Tick(In(0, &Desc462yLayer1, FetchState::None));
    C::PrepInput standing = In(100, &Desc462yLayer1, FetchState::Ready);
    standing.can_build = false; // we stand in entry 0's dungeon (slot 0 in the Chalice)
    Check(inside.Tick(standing) == Action::None && inside.status() == Status::Working,
          "never rebuild entry 0 under our feet: the build waits");
    standing.now_ms = 200;
    standing.can_build = true; // we followed the host out
    Check(inside.Tick(standing) == Action::BuildEntry, "out of it: built");

    C::GuestPrep prep;
    prep.Tick(In(0, &Desc6s46Layer3, FetchState::None, 9233574, true));
    Check(prep.status() == Status::Ready, "ready for 6s46u7i4");
    Check(prep.Tick(In(100, &Desc462yLayer1, FetchState::None, 9233574, true)) == Action::StartFetch &&
              prep.status() == Status::Working && prep.target() == 116979,
          "the host entered another dungeon: fetch that one");
    Check(prep.Tick(In(200, &Desc6s46Layer3, FetchState::Requesting, 9233574, true)) ==
              Action::ReleaseFetch,
          "and back: the old fetch is released");
    Check(prep.status() == Status::Ready, "entry 0 still holds 6s46u7i4: ready again");

    C::GuestPrep gone;
    gone.Tick(In(0, &Desc6s46Layer3, FetchState::None));
    Check(gone.Tick(In(100, nullptr, FetchState::Requesting)) == Action::ReleaseFetch &&
              gone.status() == Status::Idle,
          "the host left the Chalice: fetch released, idle");

    C::GuestPrep switched;
    Check(switched.Tick(In(0, &Desc6s46Layer3, FetchState::None)) == Action::StartFetch,
          "fetching 6s46u7i4");
    Check(switched.Tick(In(100, &Desc462yLayer1, FetchState::Requesting)) == Action::ReleaseFetch,
          "the host entered 462yygcg meanwhile: the 6s46u7i4 fetch is released");
    Check(switched.Tick(In(200, &Desc462yLayer1, FetchState::None)) == Action::StartFetch &&
              switched.target() == 116979,
          "then 462yygcg is fetched");
    Check(switched.Tick(In(300, &Desc462yLayer1, FetchState::Ready)) == Action::BuildEntry,
          "and built from its own fetch");

    C::GuestPrep failed;
    const Descriptor offline = WithChannel(Desc6s46Layer3, C::OfflineChannel);
    failed.Tick(In(0, &offline, FetchState::None));
    Check(failed.status() == Status::Failed, "failed for the offline dungeon");
    Check(failed.Tick(In(100, &Desc462yLayer1, FetchState::None)) == Action::StartFetch,
          "a new host dungeon gets a fresh start");
}

void FollowGate() {
    using C::Gate;
    Check(C::FollowGate(DreamMap, Status::Idle, 0) == Gate::Go, "outside the Chalice: follow as before");
    Check(C::FollowGate(ChaliceMap, Status::Ready, 0) == Gate::Go, "host dungeon ready: follow");
    Check(C::FollowGate(ChaliceMap, Status::Working, 1000) == Gate::Wait, "being prepared: wait");
    Check(C::FollowGate(ChaliceMap, Status::Idle, 1000) == Gate::Wait,
          "the host's dungeon not heard yet: wait");
    Check(C::FollowGate(ChaliceMap, Status::Working, C::MaxPrepWaitMs) == Gate::Refuse,
          "not ready in time: do not follow into a dungeon we would build wrong");
    Check(C::FollowGate(ChaliceMap, Status::Failed, 0) == Gate::Refuse, "failed: do not follow");
}

void GuestLoad() {
    auto plan = C::PlanGuestLoad(ChaliceMap, 3, true, 0, Status::Ready);
    Check(plan.apply && plan.enter_session_bank,
          "summon-phase load into the Chalice, first in the session: session bank, then slot 0");
    plan = C::PlanGuestLoad(ChaliceMap, 3, true, 1, Status::Ready);
    Check(plan.apply && !plan.enter_session_bank, "already in the session bank: slot 0 only");
    Check(!C::PlanGuestLoad(DreamMap, 3, true, 1, Status::Ready).apply, "not a Chalice load: nothing");
    Check(!C::PlanGuestLoad(ChaliceMap, 0, true, 1, Status::Ready).apply,
          "not a summon-phase load (our own world): nothing");
    Check(!C::PlanGuestLoad(ChaliceMap, 3, false, 1, Status::Ready).apply, "not a guest: nothing");
    Check(!C::PlanGuestLoad(ChaliceMap, 3, true, 1, Status::Working).apply,
          "host dungeon not ready: nothing (and BBSC says so)");
}

void SelfTest() {
    const auto test = C::ParseSelfTest("9233574:3");
    Check(test.has_value() && test->channel == 9233574 && test->layer == 2,
          "self-test \"9233574:3\" = channel 9233574, layer 3 (0-based 2)");
    Check(!C::ParseSelfTest("").has_value() && !C::ParseSelfTest("9233574").has_value() &&
              !C::ParseSelfTest(":3").has_value() && !C::ParseSelfTest("abc:3").has_value() &&
              !C::ParseSelfTest("9233574:0").has_value() && !C::ParseSelfTest("9233574:9").has_value() &&
              !C::ParseSelfTest("9233574:3x").has_value(),
          "anything else is refused");
    const Descriptor target = C::SelfTestTarget(test.value_or(C::SelfTest{}));
    Check(C::Channel(target) == 9233574 && C::Layer(target) == 2 && C::Fetchable(target),
          "the self-test target is fetchable");
    using C::SelfTestStep;
    Check(C::PlanSelfTestLoad(ChaliceMap, 0, Status::Ready, false) == SelfTestStep::Apply,
          "solo Chalice load with entry 0 ready: apply");
    Check(C::PlanSelfTestLoad(ChaliceMap, 0, Status::Working, false) == SelfTestStep::None,
          "entry 0 not ready: the altar's own dungeon");
    Check(C::PlanSelfTestLoad(ChaliceMap, 6, Status::Ready, false) == SelfTestStep::None,
          "as a guest the real path owns it");
    Check(C::PlanSelfTestLoad(DreamMap, 0, Status::Ready, true) == SelfTestStep::Restore,
          "the next load out of the Chalice puts our own slot back");
    Check(C::PlanSelfTestLoad(DreamMap, 6, Status::Ready, true) == SelfTestStep::Restore,
          "even when a session started meanwhile");
    Check(C::PlanSelfTestLoad(DreamMap, 0, Status::Ready, false) == SelfTestStep::None,
          "nothing applied, nothing to restore");
    Check(C::PlanSelfTestLoad(ChaliceMap, 0, Status::Ready, true) == SelfTestStep::Apply,
          "a death reload in the dungeon applies again");
}

} // namespace

int main() {
    SlotRule();
    DescriptorFields();
    DescriptorFromEntry();
    Packet();
    HostSide();
    GuestPrepHappyPath();
    GuestPrepAlreadyThere();
    GuestPrepOffline();
    GuestPrepRetries();
    GuestPrepHostMoves();
    FollowGate();
    GuestLoad();
    SelfTest();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
