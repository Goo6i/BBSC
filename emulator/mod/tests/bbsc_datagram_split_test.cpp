// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for core/bbsc_datagram_split.h. Plain C++20, no emulator headers:
//   g++ -std=c++20 -I emulator/src emulator/mod/tests/bbsc_datagram_split_test.cpp -o ds_test && ./ds_test
//   clang-cl /std:c++20 /EHsc /I emulator\src emulator\mod\tests\bbsc_datagram_split_test.cpp

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/bbsc_datagram_split.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

namespace Split = Bbsc::DatagramSplit;
using Bytes = std::vector<std::uint8_t>;

// One record as the game writes it: [channel 0-3][length, big endian][payload].
void Record(Bytes& out, std::uint8_t channel, std::size_t payload, std::uint8_t fill = 0xAB) {
    out.push_back(channel);
    out.push_back(static_cast<std::uint8_t>(payload >> 8));
    out.push_back(static_cast<std::uint8_t>(payload & 0xFF));
    for (std::size_t i = 0; i < payload; ++i) {
        out.push_back(static_cast<std::uint8_t>(fill + i));
    }
}

Split::Plan PlanOf(const Bytes& data, std::size_t limit = Split::DefaultLimit) {
    return Split::PlanSplit(data.data(), data.size(), limit);
}

// The pieces cover the datagram exactly, in order, without gaps or overlaps.
bool Covers(const Split::Plan& plan, std::size_t size) {
    std::size_t next = 0;
    for (const Split::Piece& piece : plan.pieces) {
        if (piece.offset != next || piece.size == 0) {
            return false;
        }
        next += piece.size;
    }
    return next == size;
}

// Every piece starts on a record and holds whole records only.
bool OnRecordBoundaries(const Split::Plan& plan, const Bytes& data) {
    for (const Split::Piece& piece : plan.pieces) {
        std::size_t at = piece.offset;
        const std::size_t end = piece.offset + piece.size;
        while (at < end) {
            if (end - at < Split::RecordHeader || data[at] > 3) {
                return false;
            }
            at += Split::RecordHeader + ((std::size_t{data[at + 1]} << 8) | data[at + 2]);
        }
        if (at != end) {
            return false;
        }
    }
    return true;
}

// No piece is over the limit unless it is one record that is over it by itself, and no piece
// could have taken the next record too (the packing does not waste datagrams).
bool PackedTightly(const Split::Plan& plan, const Bytes& data, std::size_t limit) {
    for (std::size_t i = 0; i < plan.pieces.size(); ++i) {
        const Split::Piece& piece = plan.pieces[i];
        const std::size_t first_record =
            Split::RecordHeader + ((std::size_t{data[piece.offset + 1]} << 8) | data[piece.offset + 2]);
        if (piece.size > limit && piece.size != first_record) {
            return false;
        }
        if (i + 1 < plan.pieces.size()) {
            const Split::Piece& next = plan.pieces[i + 1];
            const std::size_t next_record =
                Split::RecordHeader + ((std::size_t{data[next.offset + 1]} << 8) | data[next.offset + 2]);
            if (piece.size + next_record <= limit) {
                return false;
            }
        }
    }
    return true;
}

// 2026-10-30 14:55, host after a lamp trip with a guest: the game's reliable channel re-sent its
// backlog as 2.9 KB datagrams ("sent 18 (52100 bytes)" per 10 s); every one was lost on the 5G
// uplink (only the first IP fragment reached the server) and the backlog grew to 13,000.
void ABacklogDatagramIsSplitBelowTheLimit() {
    Bytes data;
    for (int i = 0; i < 41; ++i) {
        Record(data, i % 2 == 0 ? 1 : 3, 67, static_cast<std::uint8_t>(i)); // 70 bytes each
    }
    Record(data, 0, 6); // 2879 bytes in all
    const Split::Plan plan = PlanOf(data);
    Check(plan.records, "a run of the game's records is recognised as one");
    Check(plan.pieces.size() == 3, "2879 bytes of 70-byte records go out as 3 datagrams of <= 1100");
    Check(Covers(plan, data.size()), "the pieces cover the datagram exactly, in order");
    Check(OnRecordBoundaries(plan, data), "every piece holds whole records only");
    Check(PackedTightly(plan, data, Split::DefaultLimit), "no piece is over 1100, none could take more");
    Check(plan.largest_record == 70, "the largest record is reported (70 bytes)");
    Check(plan.oversized == 0, "no record is over the limit by itself");
}

void ASmallDatagramStaysOnePiece() {
    Bytes data;
    Record(data, 0, 88); // "00 00 58 ..." from the 14:55:21 log
    Record(data, 1, 35); // "01 00 23 ..."
    const Split::Plan plan = PlanOf(data);
    Check(plan.records, "a small datagram of records is recognised");
    Check(plan.pieces.size() == 1 && plan.pieces[0].offset == 0 &&
              plan.pieces[0].size == data.size(),
          "a datagram under the limit is one piece: sent unchanged");
}

void ExactlyTheLimitIsOnePiece() {
    Bytes data;
    Record(data, 1, 547);
    Record(data, 1, 547); // 1100 bytes
    Check(data.size() == Split::DefaultLimit, "(test setup: 1100 bytes)");
    const Split::Plan plan = PlanOf(data);
    Check(plan.pieces.size() == 1, "a datagram of exactly the limit is not split");
    data.push_back(0);
    data.push_back(0);
    data.push_back(0); // one empty record more: 1103
    const Split::Plan over = PlanOf(data);
    Check(over.pieces.size() == 2 && over.pieces[0].size == 1100 && over.pieces[1].size == 3,
          "one byte over the limit splits after the last record that fits");
}

void ARecordOverTheLimitGoesAlone() {
    Bytes data;
    Record(data, 1, 100);
    Record(data, 1, 1500); // 1503 bytes: cannot be split, a datagram of its own
    Record(data, 3, 100);
    const Split::Plan plan = PlanOf(data);
    Check(plan.records, "a datagram with one big record is still recognised");
    Check(plan.pieces.size() == 3, "small, big alone, small");
    Check(Covers(plan, data.size()) && OnRecordBoundaries(plan, data), "pieces still cover it on records");
    Check(plan.pieces.size() == 3 && plan.pieces[1].size == 1503,
          "the big record keeps its own datagram, unchanged");
    Check(plan.oversized == 1 && plan.largest_record == 1503, "it is counted as over the limit");
}

void BigRecordsFirstAndInARow() {
    Bytes data;
    Record(data, 1, 1300); // 1303, first in the datagram
    Record(data, 3, 1400); // 1403, right after
    Record(data, 0, 10);
    const Split::Plan plan = PlanOf(data);
    Check(plan.records && plan.pieces.size() == 3, "big first, big again, small: 3 datagrams");
    Check(Covers(plan, data.size()), "no empty datagram before or between big records");
    Check(plan.oversized == 2, "both big records are counted");
}

void NotTheGamesRecordsIsLeftAlone() {
    Bytes wrong_channel;
    Record(wrong_channel, 1, 600);
    Record(wrong_channel, 4, 600); // channel 4 does not exist (the receive side drops 4+)
    Check(!PlanOf(wrong_channel).records && PlanOf(wrong_channel).pieces.empty(),
          "a channel byte over 3 means not the game's records: send unchanged");

    Bytes truncated;
    Record(truncated, 1, 700);
    Record(truncated, 1, 700);
    truncated.resize(truncated.size() - 5); // the last length runs past the end
    Check(!PlanOf(truncated).records, "a length running past the end is not split");

    Bytes trailing;
    Record(trailing, 1, 700);
    Record(trailing, 1, 700);
    trailing.push_back(0);
    trailing.push_back(0); // 2 bytes the receiver would ignore: we do not guess
    Check(!PlanOf(trailing).records, "bytes after the last record mean: send unchanged");

    Check(!Split::PlanSplit(nullptr, 0, Split::DefaultLimit).records, "nothing to send: no plan");
    const std::uint8_t none[1] = {0};
    Check(!Split::PlanSplit(none, 0, Split::DefaultLimit).records &&
              Split::PlanSplit(none, 0, Split::DefaultLimit).pieces.empty(),
          "an empty datagram: no plan either");
}

void LengthIsBigEndian() {
    Bytes data;
    Record(data, 1, 0x0102); // 258: little endian would read 513 and run past the end
    Record(data, 1, 0x0102);
    Record(data, 1, 0x0102);
    Record(data, 1, 0x0102);
    Record(data, 1, 0x0102); // 5 x 261 = 1305
    const Split::Plan plan = PlanOf(data);
    Check(plan.records && plan.pieces.size() == 2 && plan.pieces[0].size == 4 * 261,
          "lengths are read big endian, as the game's receive code does");
}

void EmptyRecordsAreRecords() {
    Bytes data;
    for (int i = 0; i < 400; ++i) {
        Record(data, 2, 0); // 1200 bytes of empty records
    }
    const Split::Plan plan = PlanOf(data);
    Check(plan.records && plan.pieces.size() == 2 && Covers(plan, data.size()),
          "empty records count too (3 bytes each)");
}

void AnotherLimit() {
    Bytes data;
    for (int i = 0; i < 10; ++i) {
        Record(data, 1, 97); // 100 bytes each
    }
    const Split::Plan plan = PlanOf(data, 250);
    Check(plan.pieces.size() == 5 && PackedTightly(plan, data, 250), "the limit is the caller's");
}

} // namespace

int main() {
    ABacklogDatagramIsSplitBelowTheLimit();
    ASmallDatagramStaysOnePiece();
    ExactlyTheLimitIsOnePiece();
    ARecordOverTheLimitGoesAlone();
    BigRecordsFirstAndInARow();
    NotTheGamesRecordsIsLeftAlone();
    LengthIsBigEndian();
    EmptyRecordsAreRecords();
    AnotherLimit();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
