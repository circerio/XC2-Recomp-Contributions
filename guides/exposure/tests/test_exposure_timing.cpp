// The timing rules of exposure_timing.hpp, on synthetic frame times.
// SPDX-License-Identifier: MIT
#include "exposure_timing.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace exposure_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static constexpr double kBase = 1.0 / 30.0;  // the frame time the loop was tuned for
static constexpr double kK = 0.1;            // its fixed fraction per frame

enum class Method { per_frame, scaled, tick };

// Seconds until the exposure has covered 90% of a step in the measurement (log 0 -> log 1).
static double time_to_90(Method m, const std::vector<double>& frames) {
    double adapted = 0.0, t = 0.0;
    Tick tick;
    for (double dt : frames) {
        t += dt;
        if (m == Method::per_frame) adapted = adapt_step(adapted, 1.0, kK);
        if (m == Method::scaled) adapted = adapt_step_dt(adapted, 1.0, kK, dt, kBase);
        if (m == Method::tick && tick.advance(dt)) adapted = adapt_step(adapted, 1.0, kK);
        if (adapted >= 0.9) return t;
    }
    return -1.0;
}

static std::vector<double> steady(double fps, int frames) { return std::vector<double>(frames, 1.0 / fps); }

// Uneven frame times between 5 and 20 ms, as when a cap is not reached every frame.
static std::vector<double> uneven(int frames) {
    std::vector<double> out;
    unsigned state = 2024;
    for (int i = 0; i < frames; i++) {
        state = state * 1664525u + 1013904223u;
        out.push_back(0.005 + 0.015 * ((state >> 8) % 1000) / 1000.0);
    }
    return out;
}

int main() {
    const double at30 = time_to_90(Method::per_frame, steady(30, 1000));
    CHECK(at30 > 0.7 && at30 < 0.75);  // 22 frames of 1/30 s

    {  // The usual loop adapts in frames: twice the frame rate, half the time.
        CHECK(std::fabs(time_to_90(Method::per_frame, steady(60, 1000)) - at30 / 2) < 1e-9);
        CHECK(time_to_90(Method::per_frame, steady(144, 1000)) < at30 / 4);
    }
    {  // Both fixes take the same real time as at 30 fps, within one frame.
        for (double fps : {60.0, 120.0, 144.0, 165.0, 240.0}) {
            CHECK(std::fabs(time_to_90(Method::scaled, steady(fps, 5000)) - at30) <= 1.0 / fps + 1e-9);
            CHECK(std::fabs(time_to_90(Method::tick, steady(fps, 5000)) - at30) <= 1.0 / fps + 1e-9);
        }
        const std::vector<double> frames = uneven(5000);
        CHECK(std::fabs(time_to_90(Method::scaled, frames) - at30) <= 0.020 + 1e-9);
        CHECK(std::fabs(time_to_90(Method::tick, frames) - at30) <= 0.020 + 1e-9);
    }
    {  // The scaled step is exact for a target that holds still: two half frames land where one whole frame does.
        const double one = adapt_step(0.0, 1.0, kK);
        const double two = adapt_step_dt(adapt_step_dt(0.0, 1.0, kK, kBase / 2, kBase), 1.0, kK, kBase / 2, kBase);
        CHECK(std::fabs(one - two) < 1e-12);
    }
    {  // The tick runs 30 times a real second at any frame rate above 30, even when it is not a multiple of 30.
        for (double fps : {60.0, 144.0, 165.0, 240.0}) {
            Tick tick;
            int ticks = 0;
            for (int i = 0; i < static_cast<int>(fps * 10); i++) ticks += tick.advance(1.0 / fps);
            CHECK(ticks >= 299 && ticks <= 300);
        }
    }
    {  // When the steps land: the k-th at k/30 s at 60 fps, and never early or more than one frame late at 144 and 165.
        for (double fps : {60.0, 144.0, 165.0}) {
            Tick tick;
            int k = 0, wrong = 0;
            for (int i = 1; i <= static_cast<int>(fps * 10); i++) {
                if (!tick.advance(1.0 / fps)) continue;
                k++;
                const double t = i / fps, due = k / 30.0;
                if (fps == 60.0) wrong += std::fabs(t - due) > 1e-9;
                else wrong += t < due - 1e-9 || t - 1.0 / fps >= due - 1e-9;
            }
            CHECK(wrong == 0);
        }
    }
    {  // At exactly 30 fps it ticks every frame (rounding does not drop one), so it matches the original step for step.
        Tick tick;
        double a = 0.0, b = 0.0;
        for (int i = 0; i < 1000; i++) {
            const double target = std::sin(i * 0.05);
            a = adapt_step(a, target, kK);
            if (tick.advance(kBase)) b = adapt_step(b, target, kK);
        }
        CHECK(a == b);
    }
    {  // A frame longer than a tick gives one tick, not two: time is lost, as in a 30 fps game that slows down.
        Tick tick;
        CHECK(tick.advance(0.1));
        CHECK(!tick.advance(0.001));
    }
    {  // Slots and late GPU results. Each frame, the update reads the result that has come back and then advances on a
       // step; the render side measures the current slot. Every result should be credited to the slot it measured.
        constexpr int kSlots = 11, kLatency = 2;
        const auto truth = [](int slot) { return 100.0 + slot; };
        for (double fps : {30.0, 60.0, 144.0}) {
            Tick tick;
            DelayedResults results(kLatency);
            int slot = 0, wrong_by_tag = 0, wrong_by_offset = 0;
            for (int frame = 0; frame < 2000; frame++) {
                Result r;
                if (results.take(r)) {
                    wrong_by_tag += r.value != truth(r.slot);
                    wrong_by_offset += r.value != truth(slot_by_frame_offset(slot, kLatency, kSlots));
                }
                const bool step = fps <= 30.0 || tick.advance(1.0 / fps);
                if (step) slot = (slot + 1) % kSlots;
                results.record(slot, truth(slot));
            }
            CHECK(wrong_by_tag == 0);
            if (fps <= 30.0) CHECK(wrong_by_offset == 0);  // one frame = one step: the offset is right
            else CHECK(wrong_by_offset > 0);               // steps held: the offset credits the wrong slots
        }
    }

    if (failures == 0) std::printf("all exposure timing checks passed\n");
    return failures == 0 ? 0 : 1;
}
