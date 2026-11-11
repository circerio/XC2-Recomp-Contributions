// How long auto exposure takes to adapt at different frame rates, with the usual per-frame loop and with each fix.
// Synthetic: a step in the measured brightness, a loop tuned for 30 fps. It shows the mechanism and proves nothing about
// any real game. What we saw in a real one is in guides/exposure/README.md.
// Exit 1 if either fix is off by more than one frame from 30 fps.
// SPDX-License-Identifier: MIT
#include "exposure_timing.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace exposure_guide;

static constexpr double kBase = 1.0 / 30.0;
static constexpr double kK = 0.1;

// Seconds until the exposure has covered 90% of a step from log 0 to log 1; mode 0 per frame, 1 scaled, 2 tick.
static double time_to_90(int mode, const std::vector<double>& frames) {
    double adapted = 0.0, t = 0.0;
    Tick tick;
    for (double dt : frames) {
        t += dt;
        if (mode == 0) adapted = adapt_step(adapted, 1.0, kK);
        if (mode == 1) adapted = adapt_step_dt(adapted, 1.0, kK, dt, kBase);
        if (mode == 2 && tick.advance(dt)) adapted = adapt_step(adapted, 1.0, kK);
        if (adapted >= 0.9) return t;
    }
    return -1.0;
}

int main() {
    std::printf("time to cover 90%% of a brightness step, seconds (loop tuned for 30 fps)\n");
    std::printf("frame rate      per frame   scaled step   30 Hz tick\n");
    const double at30 = time_to_90(0, std::vector<double>(1000, kBase));
    bool ok = true;
    for (double fps : {30.0, 60.0, 120.0, 144.0, 165.0, 240.0}) {
        const std::vector<double> frames(5000, 1.0 / fps);
        const double a = time_to_90(0, frames), b = time_to_90(1, frames), c = time_to_90(2, frames);
        std::printf("%10.0f %13.3f %13.3f %12.3f\n", fps, a, b, c);
        ok = ok && std::fabs(b - at30) <= 1.0 / fps + 1e-9 && std::fabs(c - at30) <= 1.0 / fps + 1e-9;
    }
    return ok ? 0 : 1;
}
