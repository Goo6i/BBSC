// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// The game's datagrams, cut at record boundaries so none needs IP fragmentation.
//
// The game's UDP library (CUSA03173 01.09: P2P socket send 0x02B19360 -> sceNetSendto, the
// connection's record send 0x02B1A7C0, receive 0x02B16CD0 / 0x02B1AA60, Ghidra addresses) packs
// records [channel 0-3][length, 2 bytes big endian][payload] back to back into one datagram, with
// no header of its own, until its size limit (about 3000 bytes) is reached. The receiver reads
// records from byte 0 to the end, one by one, so any run of whole records is a datagram it reads
// the same way, and an unchanged game on the other side needs nothing new.
//
// 2026-10-30: a host's reliable channel re-sent its backlog in 2.9 KB datagrams after a guest came
// in. A UDP payload over 1472 bytes leaves as two IP fragments, and his 5G uplink delivered only
// the first: every re-send was lost again, the backlog grew to 13,000 packets and the host's frame
// rate fell from 97 to 29 until the guest left. Datagrams of at most DefaultLimit bytes stay under
// 1200 on the wire with the emulator's P2P header (<= 18 bytes), which passes a 1280-byte tunnel.

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Bbsc::DatagramSplit {

inline constexpr std::size_t RecordHeader = 3; // channel + big-endian length
inline constexpr std::size_t DefaultLimit = 1100;
inline constexpr std::uint8_t Channels = 4; // the receive side drops a channel byte of 4 or more

struct Piece {
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct Plan {
    bool records = false;      // data is exactly a run of whole records
    std::vector<Piece> pieces; // in order; one piece = send as is; empty when !records
    std::size_t largest_record = 0;
    std::size_t oversized = 0; // records over the limit by themselves (each sent alone)
};

// Packs the records of data, in order, into as few pieces of at most `limit` bytes as greedy
// packing gives. A record over the limit gets a piece of its own (it cannot be cut). When data is
// not exactly a run of records (a channel byte of 4+, a length past the end, bytes left over) the
// plan has records == false and no pieces: the caller sends data unchanged.
inline Plan PlanSplit(const std::uint8_t* data, std::size_t size, std::size_t limit) {
    Plan plan;
    if (data == nullptr || size == 0) {
        return plan;
    }
    std::vector<Piece> pieces;
    std::size_t at = 0;
    Piece open{};
    while (at < size) {
        if (size - at < RecordHeader || data[at] >= Channels) {
            return plan;
        }
        const std::size_t length = (std::size_t{data[at + 1]} << 8) | data[at + 2];
        const std::size_t record = RecordHeader + length;
        if (record > size - at) {
            return plan;
        }
        if (record > plan.largest_record) {
            plan.largest_record = record;
        }
        if (record > limit) {
            ++plan.oversized;
        }
        if (open.size != 0 && open.size + record > limit) {
            pieces.push_back(open);
            open = {};
        }
        if (open.size == 0) {
            open.offset = at;
        }
        open.size += record;
        at += record;
    }
    pieces.push_back(open);
    plan.records = true;
    plan.pieces = std::move(pieces);
    return plan;
}

} // namespace Bbsc::DatagramSplit
