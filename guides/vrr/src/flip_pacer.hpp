// Keeps a schedule for the flips themselves (the vkQueuePresentKHR calls), on the thread that makes them.
// Reference code for guides/vrr/README.md. No clock of its own, so it can be unit tested. Typical use, right before the
// present call:
//
//     uint64_t now = monotonic_ns();
//     if (uint64_t hold = pacer.hold_ns(now)) {
//         if (hold > kSpinNs) sleep_ns(hold - kSpinNs);          // the OS timer may wake late, so spin the rest
//         while ((now = monotonic_ns()) < pacer.due_ns()) yield();
//     }
//     pacer.flipping(now, period_ns);
//     vkQueuePresentKHR(queue, &present_info);
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace vrr_guide {

class FlipPacer {
public:
    // A flip up to this late keeps the schedule, so that a timer waking a little late does not lower the frame rate.
    // Later than this and the schedule starts anew from the late flip, instead of shortening the next interval.
    static constexpr uint64_t kDefaultLateLimitNs = 2'000'000;

    explicit FlipPacer(uint64_t late_limit_ns = kDefaultLateLimitNs) : late_limit_ns_(late_limit_ns) {}

    // How long the flip that is ready now has to wait.
    uint64_t hold_ns(uint64_t now_ns) const { return due_ns_ && now_ns < due_ns_ ? due_ns_ - now_ns : 0; }

    // When the next flip is due; 0 if there is no schedule yet.
    uint64_t due_ns() const { return due_ns_; }

    // Call at the moment of the flip. period_ns 0 (no cap) turns the schedule off.
    void flipping(uint64_t now_ns, uint64_t period_ns) {
        if (period_ns == 0) {
            due_ns_ = 0;
            return;
        }
        const bool on_schedule = due_ns_ != 0 && now_ns <= due_ns_ + late_limit_ns_;
        due_ns_ = (on_schedule ? due_ns_ : now_ns) + period_ns;
    }

private:
    uint64_t late_limit_ns_;
    uint64_t due_ns_ = 0;
};

}  // namespace vrr_guide
