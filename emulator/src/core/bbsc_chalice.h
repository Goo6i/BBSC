// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC: Chalice Dungeons together. Plain logic, no game memory, so it is tested on its own
// (mod/tests/bbsc_chalice_test.cpp). Addresses below are eboot image offsets (BBSC's base).
//
// Which dungeon a Chalice load builds. Every Chalice Dungeon is packed map 0x1D000000; the game
// keeps up to eight dungeons in SprjHolygrail [0x0553E9E8] + 8 (entries 1..5 = the altar's, in the
// save's order, entry 0 = the dungeon of a world being visited). The in-game step every map load
// builds (0x01937570) reads event flags 9020..9026 and picks entry k for flag 9020+k (its reader
// 0x013CFD80 returns the flags last-first, hence 7 - bit length). The load request it is given
// (rcx) carries a dungeon descriptor at +0xA0: channel id, dungeon map id, the nine feature pairs,
// a per-chalice value, the layer (0-based) - written by the altar when the player enters. The step
// copies only +0x0C..+0x9E of the request, so the descriptor is read later in the load.
//
// The bug: BBSC's follow, join and wipe reloads only say "map 0x1D000000". Each guest's game then
// built ITS OWN selected dungeon on ITS OWN last layer under the host's coordinates, and a guest
// without the host's dungeon fell through the floor (2026-10-29, glyph 6s46u7i4 layer 3).
//
// The fix copies vanilla's own way into a host's dungeon (0x014BB200): the host's dungeon is
// fetched by channel id from the game server (0x01E65290, POST /channel/get_info; channel object
// +0x114: 1 requesting, 3 ready, 0 or 2 failed) and built into entry 0 (0x01AC3EF0, returns 1
// when entry 0 reached state 5). On the guest's summon-phase load into the Chalice the game's
// flag bank switch (0x013BB590, mode at flag manager +0x80) is put into session mode, slot 0 is
// selected (0x013B89B0: sets 9020, points the dungeon's event flags at slot 0's own storage) and
// the host's descriptor is written into the request. Session mode keeps the guest's own altar
// selection untouched; the game's switch back to mode 0 when the session ends restores it.
//
// The host sends its descriptor (relay "SHAD" 0x28) once a second while it hosts in a Chalice.

#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace Bbsc::Chalice {

constexpr std::uint32_t Area = 0x1D;
constexpr bool IsChaliceMap(std::uint32_t packed_map) {
    return (packed_map >> 24) == Area;
}

// The in-game step: entry [7 - bit length] of flags 9020..9026 as the game's reader returns them.
constexpr int SlotOf(std::uint32_t game_value) {
    int bits = 0;
    for (; game_value != 0; game_value >>= 1) {
        ++bits;
    }
    return 7 - bits;
}
constexpr std::uint32_t SlotFlag(int slot) {
    return 9020u + static_cast<std::uint32_t>(slot);
}

// ---- The dungeon descriptor (load request + 0xA0 .. + 0xFF) ----
constexpr std::size_t RequestDescriptorOffset = 0xA0;
constexpr std::size_t DescriptorSize = 0x60;
using Descriptor = std::array<std::uint8_t, DescriptorSize>;
constexpr std::size_t DescChannel = 0x00;  // u64
constexpr std::size_t DescDungeon = 0x08;  // u32 dungeon map id (0x1Dxxxxxx)
constexpr std::size_t DescPairs = 0x0C;    // 9 x (u32 param id, u32 value)
constexpr std::size_t PairsSize = 0x48;
constexpr std::size_t DescValue = 0x54;    // u32, per chalice (100000, 800000, ...)
constexpr std::size_t DescLayer = 0x58;    // s32, 0-based
constexpr std::uint64_t OfflineChannel = 0x8000000000000000ull; // a dungeon never put online
constexpr std::int32_t MaxLayer = 3;                            // four layers at most

// The same fields in a Holygrail entry.
constexpr std::size_t EntryChannel = 0x1E0;
constexpr std::size_t EntryDungeonMap = 0x1E8;
constexpr std::size_t EntryPairs = 0x200;
constexpr std::size_t EntryValue = 0x350;
constexpr std::size_t EntryBytes = 0x360; // how much of an entry FromEntry reads

template <typename T>
T Get(const Descriptor& d, std::size_t at) {
    T value{};
    std::memcpy(&value, d.data() + at, sizeof(T));
    return value;
}

inline std::uint64_t Channel(const Descriptor& d) {
    return Get<std::uint64_t>(d, DescChannel);
}
inline std::uint32_t DungeonMap(const Descriptor& d) {
    return Get<std::uint32_t>(d, DescDungeon);
}
inline std::int32_t Layer(const Descriptor& d) {
    return Get<std::int32_t>(d, DescLayer);
}
inline void SetLayer(Descriptor& d, std::int32_t layer) {
    std::memcpy(d.data() + DescLayer, &layer, sizeof(layer));
}
// The game can fetch it from the server: an online channel id and a Chalice dungeon.
inline bool Fetchable(const Descriptor& d) {
    const std::uint64_t channel = Channel(d);
    return channel != 0 && channel != OfflineChannel && IsChaliceMap(DungeonMap(d));
}

// A request descriptor for the dungeon in `entry` (EntryBytes readable) on `layer`, over `base`
// (the request as the game left it: what the entry does not hold stays as it was).
inline Descriptor FromEntry(const std::uint8_t* entry, const Descriptor& base, std::int32_t layer) {
    Descriptor d = base;
    std::memcpy(d.data() + DescChannel, entry + EntryChannel, 8);
    std::memcpy(d.data() + DescDungeon, entry + EntryDungeonMap, 4);
    std::memcpy(d.data() + DescPairs, entry + EntryPairs, PairsSize);
    std::memcpy(d.data() + DescValue, entry + EntryValue, 4);
    SetLayer(d, layer);
    return d;
}

// ---- Relay packet: "SHAD" 0x28, u8 version, u32 sequence, the descriptor ----
constexpr std::uint8_t PacketType = 0x28;
constexpr std::uint8_t PacketVersion = 1;
constexpr std::size_t PacketHeader = 10;
constexpr std::size_t PacketSize = PacketHeader + DescriptorSize;

struct HostDungeon {
    std::uint32_t sequence = 0;
    Descriptor descriptor{};
};

inline std::array<std::uint8_t, PacketSize> EncodePacket(std::uint32_t sequence, const Descriptor& d) {
    std::array<std::uint8_t, PacketSize> frame{'S', 'H', 'A', 'D', PacketType, PacketVersion};
    std::memcpy(frame.data() + 6, &sequence, 4);
    std::memcpy(frame.data() + PacketHeader, d.data(), DescriptorSize);
    return frame;
}

// A later version may append fields; this one reads its own part.
inline std::optional<HostDungeon> DecodePacket(const std::uint8_t* frame, std::size_t size) {
    if (frame == nullptr || size < PacketSize || std::memcmp(frame, "SHAD", 4) != 0 ||
        frame[4] != PacketType || frame[5] < PacketVersion) {
        return std::nullopt;
    }
    HostDungeon out{};
    std::memcpy(&out.sequence, frame + 6, 4);
    std::memcpy(out.descriptor.data(), frame + PacketHeader, DescriptorSize);
    if (!IsChaliceMap(DungeonMap(out.descriptor))) {
        return std::nullopt;
    }
    return out;
}

// ---- Host: the dungeon our own world is in ----
class HostKeeper {
public:
    // Every load step of our game. As someone's guest our loads say nothing about our dungeon.
    void OnLoad(std::uint32_t map, bool as_guest, const Descriptor& request_descriptor) {
        if (as_guest) {
            return;
        }
        if (IsChaliceMap(map)) {
            current_ = request_descriptor;
            ++sequence_;
        } else {
            current_.reset();
        }
    }
    // What a host tells its guests: its dungeon while it hosts and stands in it.
    std::optional<HostDungeon> ToSend(bool hosting, std::uint32_t current_map) const {
        if (!hosting || !IsChaliceMap(current_map) || !current_.has_value()) {
            return std::nullopt;
        }
        return HostDungeon{sequence_, *current_};
    }

private:
    std::optional<Descriptor> current_;
    std::uint32_t sequence_ = 0;
};

// ---- Guest: get the host's dungeon into entry 0 before loading into it ----
enum class FetchState { None, Requesting, Ready, Failed }; // the channel object we hold
enum class Action { None, StartFetch, ReleaseFetch, BuildEntry };
enum class Status { Idle, Working, Ready, Failed };

constexpr int MaxFetchAttempts = 3;
constexpr int MaxBuildAttempts = 3;
constexpr std::int64_t FetchTimeoutMs = 15'000;
constexpr std::int64_t RetryDelayMs = 2'000;

struct PrepInput {
    std::int64_t now_ms = 0;
    std::optional<Descriptor> host; // the host's dungeon (fresh), none when not in a Chalice
    std::uint64_t entry0_channel = 0;
    bool entry0_ready = false;      // state 5, not busy
    FetchState fetch = FetchState::None;
    bool network_ready = false;     // the game's network checks for a fetch pass
    bool can_build = true;          // false while we stand in entry 0's dungeon
};

class GuestPrep {
public:
    Action Tick(const PrepInput& in) {
        const auto release_or = [&](Action otherwise) {
            return in.fetch != FetchState::None ? Action::ReleaseFetch : otherwise;
        };
        if (!in.host.has_value()) {
            *this = GuestPrep{};
            return release_or(Action::None);
        }
        const std::uint64_t channel = Channel(*in.host);
        if (channel != target_) {
            *this = GuestPrep{};
            target_ = channel;
            status_ = Status::Working;
        }
        if (!Fetchable(*in.host)) {
            status_ = Status::Failed;
            reason_ = "the host's dungeon has no online id (never put online)";
            return release_or(Action::None);
        }
        if (in.entry0_ready && in.entry0_channel == channel) {
            status_ = Status::Ready;
            reason_ = "";
            return release_or(Action::None);
        }
        if (status_ == Status::Failed) {
            return release_or(Action::None); // given up on this dungeon
        }
        if (status_ == Status::Ready) {
            attempts_ = 0; // entry 0 was taken away: start over
            builds_ = 0;
        }
        status_ = Status::Working;
        if (in.fetch != FetchState::None && fetch_target_ != channel) {
            return Action::ReleaseFetch; // a fetch for a dungeon the host left
        }
        switch (in.fetch) {
        case FetchState::None:
            if (!in.network_ready || in.now_ms < next_try_ms_) {
                return Action::None;
            }
            if (attempts_ >= MaxFetchAttempts) {
                status_ = Status::Failed;
                reason_ = "the server did not send the host's dungeon";
                return Action::None;
            }
            ++attempts_;
            fetch_target_ = channel;
            fetch_started_ms_ = in.now_ms;
            return Action::StartFetch;
        case FetchState::Requesting:
            if (in.now_ms - fetch_started_ms_ > FetchTimeoutMs) {
                next_try_ms_ = in.now_ms + RetryDelayMs;
                return Action::ReleaseFetch;
            }
            return Action::None;
        case FetchState::Failed:
            next_try_ms_ = in.now_ms + RetryDelayMs;
            return Action::ReleaseFetch;
        case FetchState::Ready:
            if (!in.can_build) {
                return Action::None; // entry 0 is the dungeon we stand in: not now
            }
            if (builds_ >= MaxBuildAttempts) {
                status_ = Status::Failed;
                reason_ = "the game did not build the host's dungeon";
                return Action::ReleaseFetch;
            }
            ++builds_;
            return Action::BuildEntry;
        }
        return Action::None;
    }
    Status status() const {
        return status_;
    }
    std::uint64_t target() const {
        return target_;
    }
    const char* reason() const {
        return reason_;
    }

private:
    Status status_ = Status::Idle;
    std::uint64_t target_ = 0;
    std::uint64_t fetch_target_ = 0;
    int attempts_ = 0;
    int builds_ = 0;
    std::int64_t fetch_started_ms_ = 0;
    std::int64_t next_try_ms_ = 0;
    const char* reason_ = "";
};

// ---- Guest: follow the host into a Chalice only with its dungeon ready ----
enum class Gate { Go, Wait, Refuse };
constexpr std::int64_t MaxPrepWaitMs = 20'000;

inline Gate FollowGate(std::uint32_t host_map, Status status, std::int64_t waited_ms) {
    if (!IsChaliceMap(host_map) || status == Status::Ready) {
        return Gate::Go;
    }
    if (status == Status::Failed || waited_ms >= MaxPrepWaitMs) {
        return Gate::Refuse;
    }
    return Gate::Wait;
}

// ---- Guest: the load step of a summon-phase load into the host's Chalice ----
constexpr int SummonPhase = 3;
constexpr int SessionBank = 1;

struct LoadPlan {
    bool apply = false;              // slot 0 + the host's descriptor
    bool enter_session_bank = false; // the flag bank first, so our own selection stays untouched
};

inline LoadPlan PlanGuestLoad(std::uint32_t map, int summon_phase, bool guest, int bank_mode,
                              Status status) {
    LoadPlan plan{};
    plan.apply = IsChaliceMap(map) && summon_phase == SummonPhase && guest && status == Status::Ready;
    plan.enter_session_bank = plan.apply && bank_mode != SessionBank;
    return plan;
}

// ---- Self-test on one PC: "<channel>:<layer 1-4>" builds that dungeon on a solo Chalice load ----
struct SelfTest {
    std::uint64_t channel = 0;
    std::int32_t layer = 0; // 0-based
};

inline std::optional<SelfTest> ParseSelfTest(std::string_view text) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return std::nullopt;
    }
    SelfTest out{};
    const char* begin = text.data();
    const auto channel = std::from_chars(begin, begin + colon, out.channel);
    if (channel.ec != std::errc{} || channel.ptr != begin + colon || out.channel == 0) {
        return std::nullopt;
    }
    std::int32_t layer = 0;
    const auto layer_end = text.data() + text.size();
    const auto parsed = std::from_chars(begin + colon + 1, layer_end, layer);
    if (parsed.ec != std::errc{} || parsed.ptr != layer_end || layer < 1 || layer > MaxLayer + 1) {
        return std::nullopt;
    }
    out.layer = layer - 1;
    return out;
}

// What GuestPrep is given as "the host's dungeon" in a self-test.
inline Descriptor SelfTestTarget(const SelfTest& test) {
    Descriptor d{};
    std::memcpy(d.data() + DescChannel, &test.channel, 8);
    const std::uint32_t map = Area << 24;
    std::memcpy(d.data() + DescDungeon, &map, 4);
    SetLayer(d, test.layer);
    return d;
}

enum class SelfTestStep { None, Apply, Restore };

// Solo only (session state 0); our own slot goes back at the next load out of the Chalice.
inline SelfTestStep PlanSelfTestLoad(std::uint32_t map, int session_state, Status status,
                                     bool applied_before) {
    if (!IsChaliceMap(map)) {
        return applied_before ? SelfTestStep::Restore : SelfTestStep::None;
    }
    return session_state == 0 && status == Status::Ready ? SelfTestStep::Apply : SelfTestStep::None;
}

} // namespace Bbsc::Chalice
