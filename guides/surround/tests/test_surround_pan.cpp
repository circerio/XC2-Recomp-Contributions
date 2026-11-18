// The rules of surround_pan.hpp.
// SPDX-License-Identifier: MIT
#include "surround_pan.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace surround_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }
static bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }
static const double kHalf = std::sqrt(0.5);

// The gains of `g` in a fixed order, for sums and comparisons.
static void as_array(const Gains& g, float* v) {
    v[0] = g.fl, v[1] = g.fr, v[2] = g.sur_l, v[3] = g.sur_r, v[4] = g.back_l, v[5] = g.back_r;
}

// A positioned mono sound at `az` degrees, with the game's own gains `left` and `right`.
static Sound at(double az, float left = 0.6f, float right = 0.4f) {
    Sound s;
    s.mono = s.positioned = true;
    s.left = left, s.right = right;
    s.dir_right = float(std::sin(az * kPi / 180)), s.dir_front = float(std::cos(az * kPi / 180));
    return s;
}

static void test_channel_order() {
    // stereo FL FR
    CHECK(channel_count(kStereo) == 2 && channel_of(kStereo, kFL) == 0 && channel_of(kStereo, kFR) == 1);
    // both 5.1 masks: FL FR FC LFE and their one surround pair on 4 and 5
    for (uint32_t m : {k51Back, k51Side}) {
        CHECK(channel_count(m) == 6);
        CHECK(channel_of(m, kFL) == 0 && channel_of(m, kFR) == 1 && channel_of(m, kFC) == 2 && channel_of(m, kLFE) == 3);
    }
    CHECK(channel_of(k51Back, kBL) == 4 && channel_of(k51Back, kBR) == 5 && channel_of(k51Back, kSL) == -1);
    CHECK(channel_of(k51Side, kSL) == 4 && channel_of(k51Side, kSR) == 5 && channel_of(k51Side, kBL) == -1);
    // 7.1: the back pair before the side pair
    CHECK(channel_count(k71) == 8);
    CHECK(channel_of(k71, kBL) == 4 && channel_of(k71, kBR) == 5 && channel_of(k71, kSL) == 6 && channel_of(k71, kSR) == 7);
    CHECK(k51Back == 0x3F && k51Side == 0x60F && k71 == 0x63F);
}

static void test_choose() {
    bool un = true;
    CHECK(choose_mask(Asked::automatic, k71, &un) == k71 && !un);
    CHECK(choose_mask(Asked::automatic, k51Side, &un) == k51Side && !un);
    CHECK(choose_mask(Asked::automatic, k51Back, &un) == k51Back && !un);
    CHECK(choose_mask(Asked::automatic, kStereo, &un) == kStereo && !un);
    CHECK(choose_mask(Asked::automatic, kFL | kFR | kFC, &un) == kStereo && !un);  // no surround pair: stereo
    CHECK(choose_mask(Asked::stereo, k71, &un) == kStereo && !un);
    CHECK(choose_mask(Asked::five_one, k71, &un) == k51Side && !un);  // 5.1 on a 7.1 device: the side pair
    CHECK(choose_mask(Asked::five_one, k51Back, &un) == k51Back && !un);
    CHECK(choose_mask(Asked::seven_one, k51Side, &un) == kStereo && un);  // the device lacks it: stereo, and why
    CHECK(choose_mask(Asked::five_one, kStereo, &un) == kStereo && un);
}

static void test_route() {
    // every input its own value, to see where each lands
    Frame f;
    f.fl = 1, f.fr = 2, f.fc = 3, f.lfe = 4, f.sur_l = 5, f.sur_r = 6, f.back_l = 7, f.back_r = 8;
    float s[2] = {}, b[6] = {}, d[6] = {}, e[8] = {};
    route(f, kStereo, s);
    CHECK(same_bits(s[0], 1 + (3 + 5) * 0.707f) && same_bits(s[1], 2 + (3 + 6) * 0.707f));  // the fold, LFE dropped
    route(f, k51Back, b);
    CHECK(b[0] == 1 && b[1] == 2 && b[2] == 3 && b[3] == 4 && b[4] == 5 && b[5] == 6);
    route(f, k51Side, d);
    CHECK(d[0] == 1 && d[1] == 2 && d[2] == 3 && d[3] == 4 && d[4] == 5 && d[5] == 6);
    route(f, k71, e);
    // the game's surround pair on the sides (6, 7), the back pair on the backs (4, 5): swapped, this fails
    CHECK(e[0] == 1 && e[1] == 2 && e[2] == 3 && e[3] == 4 && e[4] == 7 && e[5] == 8 && e[6] == 5 && e[7] == 6);
}

static void test_power_and_continuity() {
    for (bool seven : {false, true}) {
        const Ring& r = seven ? kRing71 : kRing51;
        float prev[6] = {};
        for (int i = -1800; i <= 1800; i++) {
            float g[6];
            spread(float(i / 10.0), 0.8f, r, g);
            double sum = 0;
            for (int k = 0; k < r.count; k++) {
                CHECK(g[k] >= 0);
                sum += double(g[k]) * g[k];
                if (i > -1800) CHECK(std::fabs(g[k] - prev[k]) < 0.02);  // a tenth of a degree never jumps
                prev[k] = g[k];
            }
            CHECK(near(sum, 0.64));  // always the sound's own power
        }
    }
}

static void test_symmetry() {
    for (bool seven : {false, true}) {
        const Ring& r = seven ? kRing71 : kRing51;
        for (int i = 0; i <= 1800; i++) {
            float g[6], m[6];
            spread(float(i / 10.0), 1.0f, r, g);
            spread(float(-i / 10.0), 1.0f, r, m);
            // the circles are listed in order of angle, so speaker k mirrors speaker count - 1 - k
            for (int k = 0; k < r.count; k++) CHECK(near(g[k], m[r.count - 1 - k]));
        }
    }
}

static void test_on_speaker() {
    for (bool seven : {false, true}) {
        const Ring& r = seven ? kRing71 : kRing51;
        for (int k = 0; k < r.count; k++) {
            // through a direction and back, as a game's angle comes: a few float steps off, still that speaker alone
            Gains g = place(at(r.sp[k].az, 0.5f, 0.5f), true, seven ? k71 : k51Side, seven);
            float v[6];
            as_array(g, v);
            const float amp = amplitude_of(0.5f, 0.5f);
            for (int j = 0; j < 6; j++) CHECK(v[j] == (j == int(r.sp[k].spk) ? amp : 0.0f));
        }
    }
}

static void test_ahead_and_behind() {
    // straight ahead: the game's own level on each front speaker, from either circle
    for (bool seven : {false, true}) {
        Gains g = place(at(0, 0.7f, 0.7f), true, k71, seven);
        CHECK(near(g.fl, 0.7) && near(g.fr, 0.7) && g.sur_l == 0 && g.sur_r == 0 && g.back_l == 0 && g.back_r == 0);
    }
    // straight behind: the back pair in 7.1; the side pair in 5.1, or in 7.1 without a back pair
    Gains b = place(at(180), true, k71, true);
    CHECK(near(b.back_l, kHalf * amplitude_of(0.6f, 0.4f)) && near(b.back_r, b.back_l) && b.sur_l == 0 && b.sur_r == 0);
    for (bool back : {false, true}) {
        Gains s = place(at(180), true, k51Side, back);
        CHECK(near(s.sur_l, kHalf * amplitude_of(0.6f, 0.4f)) && near(s.sur_r, s.sur_l) && s.back_l == 0 && s.back_r == 0);
    }
    Gains n = place(at(180), true, k71, false);
    CHECK(near(n.sur_l, n.sur_r) && n.sur_l > 0 && n.back_l == 0 && n.back_r == 0);
}

static void test_off_passes_through() {
    // made-up gains, including ones that are not "nice" numbers
    const float vals[] = {0.0f, 1e-30f, 0.123456789f, 0.70795f, 1.0f, 3.5f, -0.0f};
    for (float l : vals)
        for (float r : vals)
            for (uint32_t mask : {kStereo, k51Back, k51Side, k71})
                for (bool on : {false, true}) {
                    Sound s = at(150, l, r);
                    // off, or at a stereo output: the game's own two, bit for bit, nothing elsewhere
                    if (!on || mask == kStereo) {
                        Gains g = place(s, on, mask, true);
                        CHECK(same_bits(g.fl, l) && same_bits(g.fr, r));
                        CHECK(g.sur_l == 0 && g.sur_r == 0 && g.back_l == 0 && g.back_r == 0);
                    }
                    // sounds that are not placed: untouched even when on
                    Sound unplaced = s, stereo_sound = s;
                    unplaced.positioned = false;
                    stereo_sound.mono = false;
                    for (const Sound& u : {unplaced, stereo_sound}) {
                        Gains g = place(u, on, mask, true);
                        CHECK(same_bits(g.fl, l) && same_bits(g.fr, r) && g.sur_l == 0 && g.back_r == 0);
                    }
                }
    // a direction that is not one: passed through
    Sound bad = at(90);
    bad.dir_right = bad.dir_front = 0;
    Gains g = place(bad, true, k71, true);
    CHECK(same_bits(g.fl, 0.6f) && same_bits(g.fr, 0.4f) && g.sur_r == 0);
    bad.dir_right = NAN;
    g = place(bad, true, k71, true);
    CHECK(same_bits(g.fl, 0.6f) && same_bits(g.fr, 0.4f) && g.sur_r == 0);
}

static void test_lfe_and_centre_never_written() {
    // every direction, every output: the placed sound mixed alone leaves centre and LFE exactly silent
    for (uint32_t mask : {k51Back, k51Side, k71})
        for (bool back : {false, true})
            for (int i = -180; i < 180; i++) {
                Gains g = place(at(i), true, mask, back);
                Frame f;
                f.fl = g.fl, f.fr = g.fr, f.sur_l = g.sur_l, f.sur_r = g.sur_r, f.back_l = g.back_l, f.back_r = g.back_r;
                float out[8] = {};
                route(f, mask, out);
                CHECK(out[channel_of(mask, kFC)] == 0 && out[channel_of(mask, kLFE)] == 0);
            }
}

int main() {
    test_channel_order();
    test_choose();
    test_route();
    test_power_and_continuity();
    test_symmetry();
    test_on_speaker();
    test_ahead_and_behind();
    test_off_passes_through();
    test_lfe_and_centre_never_written();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
