// Auto exposure (eye adaptation) written for a fixed 30 fps, run at any frame rate.
// Reference code for guides/exposure/README.md. No clock and no GPU calls of its own, so it can be unit tested.
//
// A game made for 30 fps usually moves its exposure a fixed fraction of the way toward the measured brightness once
// per frame, and advances anything else in the loop (a histogram slot, a counter) once per frame too. At 60 fps all of
// it runs twice as fast. Two ways to put it back on real time:
//
//   1. adapt_step_dt: scale the fraction by the frame time. Fixes the smoothing only.
//   2. Tick: run the whole original per-frame update on a 30 Hz tick and hold it on the frames in between. The steps
//      then come in the same sequence as at 30 fps, so nothing inside needs rescaling. Typical use, each frame:
//
//          if (cap_hz <= 30) { original_update(); }               // the original frame rate: original code, untouched
//          else if (tick.advance(frame_seconds)) { original_update(); }
//          else { hold_update(); }                                // no step; the render side measures the same slot again
//
// GPU results (a 1x1 brightness readback, occlusion query counts) come back a frame or two after they were recorded.
// Code that assumes "one frame = one step" works out which slot a result belongs to from that delay; once steps are
// held, the assumption breaks. DelayedResults carries the slot with each result instead.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <deque>

namespace exposure_guide {

// One step of the usual loop, in log space: move a fixed fraction k of the way toward the measurement.
// Called once per frame, it adapts in a number of frames, not a number of seconds.
inline double adapt_step(double log_adapted, double log_measured, double k) {
    return log_adapted + k * (log_measured - log_adapted);
}

// The same step for a frame of dt seconds, where k was tuned for frames of base_dt seconds. For a target that holds
// still, any split of the same time into frames ends in the same place as (dt / base_dt) steps of adapt_step.
inline double adapt_step_dt(double log_adapted, double log_measured, double k, double dt, double base_dt) {
    const double kk = 1.0 - std::pow(1.0 - k, dt / base_dt);
    return log_adapted + kk * (log_measured - log_adapted);
}

// A fixed-rate tick. Each frame adds its time; the frame that completes a tick says so. At 60 fps that is every second
// frame, so the k-th step lands at k/30 s as it does at 30 fps; at other rates it lands at most one frame later.
// A frame adds at most one tick: a frame longer than the tick period loses the rest, the way a 30 fps game slows down
// rather than catching up.
class Tick {
public:
    explicit Tick(double hz = 30.0) : hz_(hz) {}

    bool advance(double frame_seconds) {
        acc_ += std::min(frame_seconds * hz_, 1.0);
        if (acc_ < 1.0 - kSlack) return false;  // the slack keeps rounding from dropping a tick at exactly 30 fps
        acc_ -= 1.0;
        return true;
    }

private:
    static constexpr double kSlack = 1e-9;
    double hz_;
    double acc_ = 0.0;
};

// What the GPU measured for one slot, and which slot that was.
struct Result {
    int slot;
    double value;
};

// Results that become readable `latency` frames after they were recorded, as when the CPU reads a query or a 1x1
// readback from a frame or two ago. Call record() once per frame, then take().
class DelayedResults {
public:
    explicit DelayedResults(int latency) : latency_(latency) {}

    void record(int slot, double value) { queue_.push_back({slot, value}); }

    bool take(Result& out) {
        if (static_cast<int>(queue_.size()) <= latency_) return false;
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

private:
    int latency_;
    std::deque<Result> queue_;
};

// The slot a result belongs to if every frame was a step: `latency` steps back from the current one. True at the frame
// rate the code was written for, wrong once steps are held. Kept here to show the mistake; use Result::slot instead.
inline int slot_by_frame_offset(int current_slot, int latency, int slots) {
    return ((current_slot - latency) % slots + slots) % slots;
}

}  // namespace exposure_guide
