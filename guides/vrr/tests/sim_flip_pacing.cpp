// A small model of why a monitor's refresh meter can jump at a locked frame rate. Synthetic: it shows the mechanism and
// proves nothing about any real machine. What we saw on a real one is in guides/vrr/README.md section 5.
//
// One thread produces a frame on a fixed time grid. A second thread renders it, which takes a varying time, and then
// flips. Compared: flipping as soon as the frame is rendered, and flipping through FlipPacer.
// Exit 1 if FlipPacer does not remove the short intervals on this trace.
// SPDX-License-Identifier: MIT
#include "flip_pacer.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

using vrr_guide::FlipPacer;

static constexpr uint64_t kPeriodNs = 16'666'667;
static constexpr uint64_t kPanelFloorNs = 20'833'333;  // a panel whose range starts at 48 Hz
static constexpr uint64_t kShortNs = 10'000'000;

// A fixed trace: small jitter on every frame, and a spike on some of them, as when a turning camera brings in new work.
static std::vector<uint64_t> make_work(int frames) {
    std::vector<uint64_t> work;
    uint32_t state = 12345;
    const auto next = [&state] { return (state = state * 1664525u + 1013904223u) >> 4; };
    for (int i = 0; i < frames; i++) {
        uint64_t w = 5'000'000 + next() % 1'000'000;
        if (next() % 100 < 4) w += 3'000'000 + next() % 9'000'000;
        work.push_back(w);
    }
    return work;
}

struct Result {
    std::vector<uint64_t> intervals;
    uint64_t latency_sum = 0;  // from the grid time to the flip
    uint64_t latency_max = 0;
};

static Result run(const std::vector<uint64_t>& work, bool paced) {
    Result r;
    FlipPacer pacer;
    uint64_t prev_flip = 0;
    for (size_t i = 0; i < work.size(); i++) {
        const uint64_t grid = kPeriodNs * (i + 1);
        const uint64_t start = std::max(grid, prev_flip);  // the render thread is one thread
        uint64_t flip = start + work[i];
        if (paced) {
            flip += pacer.hold_ns(flip);
            pacer.flipping(flip, kPeriodNs);
        }
        if (i > 0) r.intervals.push_back(flip - prev_flip);
        r.latency_sum += flip - grid;
        r.latency_max = std::max(r.latency_max, flip - grid);
        prev_flip = flip;
    }
    return r;
}

static double ms(uint64_t ns) { return double(ns) / 1e6; }

static void print(const char* name, Result r) {
    std::sort(r.intervals.begin(), r.intervals.end());
    const size_t n = r.intervals.size();
    const auto shorter = std::lower_bound(r.intervals.begin(), r.intervals.end(), kShortNs) - r.intervals.begin();
    const auto longer = r.intervals.end() - std::upper_bound(r.intervals.begin(), r.intervals.end(), kPanelFloorNs);
    std::printf("%-22s %7.2f %7.2f %7.2f %7.2f %7.2f %10d %10d %9.2f %9.2f\n", name, ms(r.intervals.front()),
                ms(r.intervals[n / 100]), ms(r.intervals[n / 2]), ms(r.intervals[n - 1 - n / 100]), ms(r.intervals.back()),
                int(shorter), int(longer), ms(r.latency_sum / (n + 1)), ms(r.latency_max));
}

int main() {
    const std::vector<uint64_t> work = make_work(3600);  // 60 seconds at 60 fps
    const Result plain = run(work, false), paced = run(work, true);
    std::printf("synthetic trace, 3600 frames, period 16.67 ms, intervals in ms\n");
    std::printf("%-22s %7s %7s %7s %7s %7s %10s %10s %9s %9s\n", "", "min", "p1", "p50", "p99", "max", "<10 ms", ">20.83 ms",
                "lat avg", "lat max");
    print("flip when rendered", plain);
    print("through FlipPacer", paced);
    const uint64_t plain_min = *std::min_element(plain.intervals.begin(), plain.intervals.end());
    const uint64_t paced_min = *std::min_element(paced.intervals.begin(), paced.intervals.end());
    if (plain_min >= kShortNs || paced_min < kPeriodNs - FlipPacer::kDefaultLateLimitNs) {
        std::printf("FAIL: expected short intervals without pacing and none with it\n");
        return 1;
    }
    return 0;
}
