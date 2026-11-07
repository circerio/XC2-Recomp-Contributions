// Checks of src/hdr_math.hpp against known values and against the properties the guide relies on.
// SPDX-License-Identifier: MIT
#include "hdr_math.hpp"

#include <cmath>
#include <cstdio>

using namespace hdr_guide;

static int failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

int main() {
    // PQ: published values of ST 2084.
    check(near(pq_encode(10000), 1.0, 1e-12), "PQ of 10000 nits is 1");
    check(near(pq_encode(1000), 0.7518, 1e-3), "PQ of 1000 nits");
    check(near(pq_encode(203), 0.5807, 1e-3), "PQ of 203 nits");
    check(near(pq_encode(100), 0.5081, 1e-3), "PQ of 100 nits");
    check(pq_encode(0) < 1e-5, "PQ of 0 nits is about 0");
    for (double nits : {0.01, 1.0, 80.0, 200.0, 360.0, 550.0, 4000.0, 10000.0})
        check(near(pq_decode(pq_encode(nits)), nits, nits * 1e-9 + 1e-9), "PQ round trip");

    // The sRGB curve.
    check(near(srgb_to_linear(1.0), 1.0, 1e-12) && srgb_to_linear(0.0) == 0.0, "sRGB ends");
    check(near(srgb_to_linear(0.5), 0.2140, 1e-3), "sRGB mid grey");
    for (double v = 0; v <= 1.0; v += 0.01) check(near(linear_to_srgb(srgb_to_linear(v)), v, 1e-9), "sRGB round trip");

    // The primaries' conversion keeps white white and stays in range for BT.709 colours.
    const Rgb white = bt709_to_bt2020({1, 1, 1});
    check(near(white.r, 1, 1e-5) && near(white.g, 1, 1e-5) && near(white.b, 1, 1e-5), "white stays white in BT.2020");
    for (const Rgb& c : {Rgb{1, 0, 0}, Rgb{0, 1, 0}, Rgb{0, 0, 1}}) {
        const Rgb t = bt709_to_bt2020(c);
        check(t.r >= 0 && t.g >= 0 && t.b >= 0 && max_channel(t) <= 1, "BT.709 primaries fit in BT.2020");
    }

    // SDR white lands on the paper white, black on black, in both outputs.
    const Levels lv{360, 550};
    const Rgb hdr10_white = encode_hdr10({1, 1, 1}, lv);
    check(near(pq_decode(hdr10_white.g), 360, 0.1), "HDR10: SDR white is the paper white");
    check(near(encode_scrgb({1, 1, 1}, lv).g * 80, 360, 1e-9), "scRGB: SDR white is the paper white");
    check(pq_decode(encode_hdr10({0, 0, 0}, lv).g) < 1e-3 && encode_scrgb({0, 0, 0}, lv).g == 0, "black stays black");
    check(near(encode_scrgb({0.25, 0.25, 0.25}, lv).r * 80, 90, 1e-9), "scRGB: below white is linear in the paper white");

    // The shoulder: untouched below 1, continuous with slope 1 at 1, rising, and never past the peak.
    const double top = 550.0 / 360.0;
    check(soft_shoulder(0.5, top) == 0.5 && soft_shoulder(1.0, top) == 1.0, "the shoulder leaves the SDR range alone");
    check(near((soft_shoulder(1.0 + 1e-6, top) - 1.0) / 1e-6, 1.0, 1e-3), "the shoulder's slope at 1 is 1");
    double previous = 1.0;
    for (double l = 1.01; l < 1000; l *= 1.05) {
        const double s = soft_shoulder(l, top);
        check(s > previous - 1e-12 && s <= top, "the shoulder rises and stays below the peak");
        previous = s;
    }
    check(near(soft_shoulder(1e6, top), top, 1e-9), "the shoulder's limit is the peak");
    check(soft_shoulder(5.0, 1.0) <= 1.0 + 1e-3, "a peak at the paper white does not divide by zero");

    // The storage curve: the sRGB curve up to 1, the same value and slope at 1, invertible, and small far above 1.
    check(near(store_encode(1.0), 1.0, 1e-12), "the storage curve at 1");
    const double slope_below = (store_encode(1.0) - store_encode(1.0 - 1e-6)) / 1e-6;
    const double slope_above = (store_encode(1.0 + 1e-6) - store_encode(1.0)) / 1e-6;
    check(near(slope_below, slope_above, 1e-3), "the storage curve has no kink at 1");
    for (double l : {0.0, 0.001, 0.2, 1.0, 1.2, 2.0, 10.0, 100.0})
        check(near(store_decode(store_encode(l)), l, l * 1e-9 + 1e-9), "the storage curve round trip");
    check(store_encode(30.0) < 3.0, "a very bright value stays within about 1..3");

    // The hue: scaling keeps the ratios between channels, and clipping each channel does not.
    const Rgb orange{4.0, 2.0, 0.5};
    const Rgb kept = hue_at_white(orange);
    check(max_channel(kept) == 1.0, "scaled to 1 in the largest channel");
    check(near(kept.g / kept.r, orange.g / orange.r, 1e-12) && near(kept.b / kept.r, orange.b / orange.r, 1e-12), "the hue is kept");
    const Rgb clipped{std::min(orange.r, 1.0), std::min(orange.g, 1.0), std::min(orange.b, 1.0)};
    check(clipped.g / clipped.r > 0.99, "clipping each channel makes the same colour yellow, for comparison");

    // The brightness: continuous at SDR white, rising, and the slope is the grading's.
    check(near(highlight_gain(1.0, 0.7), 1.0, 1e-12), "no jump where a highlight starts");
    check(highlight_gain(3.0, 0.7) > highlight_gain(2.0, 0.7), "brighter scene values come out brighter");
    check(near(top_slope(1.0, 0.8), 1.5, 1e-9), "the slope's upper limit");
    check(near(top_slope(1.0, 0.95), 0.5, 1e-9), "the slope of a curve flattening at the top");
    check(near(top_slope(1.0, 1.0), 0.05, 1e-9), "the slope's lower limit: a flat top still rises a little");

    // Choosing the output.
    check(choose_output(false, {true, true}) == Output::Sdr, "off is off");
    check(choose_output(true, {true, true}) == Output::Hdr10, "HDR10 first");
    check(choose_output(true, {false, true}) == Output::ScRgb, "then scRGB");
    check(choose_output(true, {false, false}) == Output::Sdr, "else SDR");

    // The levels: a setting wins, then the system, then a fallback, and the peak is never below the paper white.
    Levels r = resolve_levels(0, 0, 360, 550);
    check(r.paper_white_nits == 360 && r.peak_nits == 550, "follow the system");
    r = resolve_levels(200, 1000, 360, 550);
    check(r.paper_white_nits == 200 && r.peak_nits == 1000, "a setting wins");
    r = resolve_levels(0, 0, 0, 0);
    check(r.paper_white_nits == 200 && r.peak_nits == 1000, "fallbacks");
    r = resolve_levels(500, 400, 0, 0);
    check(r.peak_nits == 500, "the peak is not below the paper white");

    std::printf(failures ? "%d FAILED\n" : "hdr_math: all checks passed\n", failures);
    return failures ? 1 : 0;
}
