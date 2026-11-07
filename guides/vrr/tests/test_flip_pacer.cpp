// FlipPacer's rules, on a clock the test controls.
// SPDX-License-Identifier: MIT
#include "flip_pacer.hpp"

#include <cstdio>

using vrr_guide::FlipPacer;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static constexpr uint64_t kMs = 1'000'000;
static constexpr uint64_t kPeriod = 16 * kMs;

int main() {
    {  // The first flip has nothing to wait for and starts the schedule.
        FlipPacer p;
        CHECK(p.hold_ns(100 * kMs) == 0);
        p.flipping(100 * kMs, kPeriod);
        CHECK(p.due_ns() == 116 * kMs);
    }
    {  // An early flip is held until it is due.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        CHECK(p.hold_ns(110 * kMs) == 6 * kMs);
        CHECK(p.hold_ns(116 * kMs) == 0);
    }
    {  // Slightly late (a timer's lateness): the schedule stays, so the frame rate does not drop.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        p.flipping(116 * kMs + kMs, kPeriod);
        CHECK(p.due_ns() == 132 * kMs);
        p.flipping(132 * kMs + 2 * kMs, kPeriod);  // exactly at the limit: still on schedule
        CHECK(p.due_ns() == 148 * kMs);
    }
    {  // Later than the limit: the schedule starts anew, so the next interval is a whole period, not a short one.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        const uint64_t late = 116 * kMs + 10 * kMs;
        p.flipping(late, kPeriod);
        CHECK(p.due_ns() == late + kPeriod);
        CHECK(p.hold_ns(late + 5 * kMs) == kPeriod - 5 * kMs);
    }
    {  // No cap: no schedule, nothing held; a later cap starts a new one.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        p.flipping(116 * kMs, 0);
        CHECK(p.due_ns() == 0);
        CHECK(p.hold_ns(117 * kMs) == 0);
        p.flipping(120 * kMs, kPeriod);
        CHECK(p.due_ns() == 136 * kMs);
    }
    {  // A timer that returned before the due time does not break the schedule.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        p.flipping(115 * kMs, kPeriod);
        CHECK(p.due_ns() == 132 * kMs);
    }
    {  // A changed period applies from the next flip on.
        FlipPacer p;
        p.flipping(100 * kMs, kPeriod);
        p.flipping(116 * kMs, 8 * kMs);
        CHECK(p.due_ns() == 124 * kMs);
    }

    std::printf(failures ? "%d FAILED\n" : "flip_pacer: all checks passed\n", failures);
    return failures ? 1 : 0;
}
