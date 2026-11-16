// Why jitter in the game's pixels blurs a scene drawn at a higher resolution, outside any game: one straight edge,
// drawn with one sample per scene pixel at each offset of a jitter sequence, and the samples averaged over the whole
// sequence. That average is an ideal TAA history (every frame weighted alike, no history clamp, nothing moving); a real
// TAA weights recent frames more and clamps its history to the current frame, so it does worse than this, not better.
//
// Printed for each scale: how many pixels per column are part of the edge, the error against the edge's true pixel
// coverage, and the largest jump of the picture between two frames. With the offsets in scene pixels the average comes
// close to the true coverage at every scale; with them in the game's pixels the edge spreads over about the scale's
// number of pixels: a blur, not anti-aliasing.
//
// SPDX-License-Identifier: MIT
#include "taa_jitter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace taa_jitter_guide;

static const int W = 96, H = 96, N = 32;  // picture size in scene pixels, jitter sequence length (not any game's)
static const double kSlope = 0.3, kY0 = 30.5;

static bool inside(double x, double y) { return y > kY0 + kSlope * x; }

// The Halton sequence in base b, element i (from 1): a common jitter sequence.
static double halton(int i, int b) {
    double f = 1, r = 0;
    for (; i > 0; i /= b) f /= b, r += f * (i % b);
    return r;
}

struct Result {
    double width, error, jump;
};

// k: how many scene pixels one jitter unit is. 1: offsets in scene pixels. s: offsets in the game's pixels at scale s.
static Result simulate(double k) {
    std::vector<double> ox(N), oy(N);
    for (int i = 0; i < N; i++) ox[i] = halton(i + 1, 2) - 0.5, oy[i] = halton(i + 1, 3) - 0.5;
    double jump = 0;
    for (int i = 0; i < N; i++) {
        const int j = (i + 1) % N;
        jump = std::max(jump, k * std::hypot(ox[j] - ox[i], oy[j] - oy[i]));
    }
    double err2 = 0, widths = 0;
    int n = 0, cols = 0;
    for (int x = 8; x < W - 8; x++) {
        int partial = 0;
        for (int y = 0; y < H; y++) {
            double avg = 0;
            for (int i = 0; i < N; i++) avg += inside(x + 0.5 + k * ox[i], y + 0.5 + k * oy[i]);
            avg /= N;
            if (avg > 0.02 && avg < 0.98) partial++;
            if (std::fabs(y + 0.5 - (kY0 + kSlope * (x + 0.5))) < 6) {  // a band around the edge
                double cover = 0;                                      // the true coverage, 16x16 samples
                for (int sy = 0; sy < 16; sy++)
                    for (int sx = 0; sx < 16; sx++) cover += inside(x + (sx + 0.5) / 16, y + (sy + 0.5) / 16);
                cover /= 256;
                err2 += (avg - cover) * (avg - cover), n++;
            }
        }
        widths += partial, cols++;
    }
    return {widths / cols, std::sqrt(err2 / n), jump};
}

int main() {
    int failures = 0;
    std::printf("scale  jitter in      edge width (px)  error vs coverage  largest jump (scene px)\n");
    for (float s : {1.0f, 1.5f, 2.0f, 3.0f}) {
        // rescale_offset is what the fix does: the game's offset divided by the scale, so one unit is one scene pixel.
        const double unit_fixed = s * rescale_offset(1.0f, s);
        const Result fixed = simulate(unit_fixed), game = simulate(s);
        std::printf("%5.2f  scene pixels   %15.2f  %17.3f  %10.2f\n", s, fixed.width, fixed.error, fixed.jump);
        std::printf("%5.2f  game pixels    %15.2f  %17.3f  %10.2f\n", s, game.width, game.error, game.jump);
        if (s >= 2.0f && !(game.width > fixed.width + 0.5 && game.error > 2.0 * fixed.error)) {
            std::printf("FAIL at scale %.2f: game-pixel jitter should be clearly wider and further from coverage\n", s);
            failures++;
        }
        if (s == 1.0f && !(std::fabs(game.width - fixed.width) < 1e-9 && std::fabs(game.error - fixed.error) < 1e-9)) {
            std::printf("FAIL at the game's size: both should be the same\n");
            failures++;
        }
    }
    if (failures == 0) std::printf("simulation checks passed\n");
    return failures == 0 ? 0 : 1;
}
