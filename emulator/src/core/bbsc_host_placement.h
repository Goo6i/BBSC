// SPDX-FileCopyrightText: Copyright 2026 CLS_AMG
// SPDX-License-Identifier: GPL-2.0-or-later

// BBSC: the placement a host sends with each ring (header X-ShadPS4-Bloodborne-Host-Placement and
// the aimed relay message to the rung sign's player). Plain logic, no game memory, so it is tested
// on its own (mod/tests/bbsc_host_placement_test.cpp).
//
// The game builds the summon (0x01874710) a moment before the claim goes out (0.1 to 80 ms in
// every logged ring of 2026-10-29), and the build hook decides what the claim carries. It used to
// clear our placement and put it back only when it rewrote a cross-map summon, so after a same-map
// build every later ring went out with none until the SOS status tick read our position again,
// which did not happen for minutes (2026-10-28 Hunter's Dream 8 min, 2026-10-29 Dream 4 min).
// Without a placement the rung guest does not learn who rang it: on 2026-10-29 the second guest's
// claims twice never became a join, and the third joined with "no host flags for map 0x15000000"
// and "the host that rang our sign: none known", while the first guest, rung with a placement,
// joined in 4 s with the host's world flags. Now every build keeps our own position read at that
// moment; only a build that cannot read it leaves the ring without one, and says why.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Bbsc::HostPlacement {

template <typename Placement>
class ClaimPlacement {
public:
    // Our position read on the SOS status tick. A failed read keeps the last good one.
    void OnOwnRead(const std::optional<Placement>& read, std::string_view /*result*/) {
        if (read.has_value()) {
            kept_ = read;
            why_none_ = {};
        }
    }

    // Our position read at the summon build, just before the claim. `result` is the read's
    // outcome name (ReadLocalSummonPlacement returns string literals), used when it failed.
    void OnSummonBuild(const std::optional<Placement>& read, std::string_view result) {
        kept_ = read;
        why_none_ = read.has_value() ? std::string_view{} : result;
        build_failed_ = !read.has_value();
    }

    std::optional<Placement> ForClaim() const {
        return kept_;
    }

    // Why ForClaim() is empty, for the "no placement sent" log line.
    std::string WhyNone() const {
        if (kept_.has_value()) {
            return {};
        }
        if (!build_failed_) {
            return "never read";
        }
        return "summon build: " + std::string(why_none_.empty() ? "position not read" : why_none_);
    }

private:
    std::optional<Placement> kept_;
    std::string_view why_none_;
    bool build_failed_{false};
};

} // namespace Bbsc::HostPlacement
