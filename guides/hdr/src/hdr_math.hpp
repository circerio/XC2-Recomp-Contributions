// The arithmetic of HDR output for a game that was made for SDR. Reference code for guides/hdr/README.md: no Vulkan and no OS
// calls, so all of it can be unit tested. src/present_hdr.frag and src/highlights.frag are the same formulas in GLSL.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace hdr_guide {

struct Rgb {
    double r = 0, g = 0, b = 0;
};

inline double max_channel(const Rgb& c) { return std::max(c.r, std::max(c.g, c.b)); }

template <class F>
Rgb per_channel(const Rgb& c, F f) {
    return {f(c.r), f(c.g), f(c.b)};
}

inline Rgb scaled(const Rgb& c, double k) { return {c.r * k, c.g * k, c.b * k}; }

// ---------------------------------------------------------------------------------------------------------------------
// Transfer functions

// The piecewise sRGB curve, the one Windows uses when it composes SDR content onto an HDR desktop. A pure 2.2 power would
// make the dark range differ from what the same picture looks like in SDR there.
inline double srgb_to_linear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
inline double linear_to_srgb(double l) { return l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055; }

// SMPTE ST 2084 (PQ). Encoded 0..1 for 0..10000 nits.
inline constexpr double kPqM1 = 2610.0 / 16384.0, kPqM2 = 2523.0 / 4096.0 * 128.0;
inline constexpr double kPqC1 = 3424.0 / 4096.0, kPqC2 = 2413.0 / 4096.0 * 32.0, kPqC3 = 2392.0 / 4096.0 * 32.0;

inline double pq_encode(double nits) {
    const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), kPqM1);
    return std::pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2);
}

inline double pq_decode(double v) {
    const double p = std::pow(std::clamp(v, 0.0, 1.0), 1.0 / kPqM2);
    return 10000.0 * std::pow(std::max(p - kPqC1, 0.0) / (kPqC2 - kPqC3 * p), 1.0 / kPqM1);
}

// BT.709 primaries to BT.2020 primaries (ITU-R BT.2087), on linear light.
inline Rgb bt709_to_bt2020(const Rgb& c) {
    return {0.6274040 * c.r + 0.3292820 * c.g + 0.0433136 * c.b,
            0.0690970 * c.r + 0.9195400 * c.g + 0.0113612 * c.b,
            0.0163916 * c.r + 0.0880132 * c.g + 0.8955950 * c.b};
}

// ---------------------------------------------------------------------------------------------------------------------
// Peak brightness and paper white

// A soft shoulder above SDR white, on linear light where SDR white is 1. `top` is the peak over the paper white. Below 1 the
// value is untouched, at 1 the slope is 1, and the peak is its limit, so nothing clips hard.
inline double soft_shoulder(double l, double top) {
    if (l <= 1.0) return l;
    const double room = std::max(top - 1.0, 1e-3);
    return 1.0 + room * (1.0 - std::exp(-(l - 1.0) / room));
}

// The shoulder on the largest channel, with every channel scaled by the same factor, so that the ratios between channels, and
// with them the hue, do not change. Applying soft_shoulder to each channel on its own would squeeze the largest channels most,
// and bright colours would drift toward yellow and white near the peak. We learned this from xcx-recomp's PR #103 (jmars125).
inline Rgb roll_off(const Rgb& linear, double top) {
    const double m = max_channel(linear);
    return m <= 1.0 ? linear : scaled(linear, soft_shoulder(m, top) / m);
}

struct Levels {
    double paper_white_nits = 200;
    double peak_nits = 1000;
};

// Linear light where SDR white is 1, to what an HDR10 swapchain (A2B10G10R10, HDR10_ST2084) takes.
inline Rgb encode_hdr10(const Rgb& linear, const Levels& lv) {
    const Rgb nits = scaled(roll_off(linear, lv.peak_nits / lv.paper_white_nits), lv.paper_white_nits);
    return per_channel(bt709_to_bt2020(nits), pq_encode);
}

// The same to what an scRGB swapchain (R16G16B16A16_SFLOAT, EXTENDED_SRGB_LINEAR) takes: 1.0 is 80 nits, BT.709 primaries.
inline Rgb encode_scrgb(const Rgb& linear, const Levels& lv) {
    return scaled(roll_off(linear, lv.peak_nits / lv.paper_white_nits), lv.paper_white_nits / 80.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Values above 1 in a post-processing chain that works on gamma-encoded pictures

// How a value above SDR white is stored in a float target that still holds gamma-encoded values, so that later passes written
// for 0..1, such as a sharpen filter, do not meet huge numbers. Up to 1 it is the sRGB curve. Above, the curve goes on with
// the same value and slope at 1 and grows only logarithmically. The present pass undoes it with store_decode.
inline constexpr double kStoreC = 0.5, kStoreK = 1.055 / 2.4;  // kStoreK: the slope of the sRGB curve at 1

inline double store_encode(double l) {
    return l <= 1.0 ? linear_to_srgb(l) : 1.0 + kStoreC * std::log(1.0 + kStoreK * (l - 1.0) / kStoreC);
}

inline double store_decode(double v) {
    return v <= 1.0 ? srgb_to_linear(v) : 1.0 + (kStoreC / kStoreK) * (std::exp((v - 1.0) / kStoreC) - 1.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Keeping a highlight's hue

// The exposed scene colour scaled so that its largest channel is 1: the same hue, at SDR white. Clipping each channel to 1
// instead turns a bright orange into yellow and then white as the channels hit 1 one after another.
inline Rgb hue_at_white(const Rgb& exposed) { return scaled(exposed, 1.0 / max_channel(exposed)); }

// How steeply the game's own grading rises just below white, relative to its value at white. `below` and `white` are the
// luminance of its grading at `below_at` and at 1, on linear light. Multiplying by this slope in highlight_gain makes a
// highlight rise out of SDR white with no visible edge. The limits only guard against odd grading curves.
inline double top_slope(double white, double below, double below_at = 0.9) {
    return std::clamp((white - below) / ((1.0 - below_at) * std::max(white, 1e-4)), 0.05, 1.5);
}

// The brightness of a highlight relative to its colour at SDR white. `m` is the largest exposed channel, above 1.
inline double highlight_gain(double m, double slope) { return 1.0 + slope * (m - 1.0); }

// ---------------------------------------------------------------------------------------------------------------------
// Choosing the output

enum class Output { Sdr, Hdr10, ScRgb };

// What the surface offers once VK_EXT_swapchain_colorspace is on.
struct SurfaceOffers {
    bool hdr10 = false;  // A2B10G10R10_UNORM_PACK32 with HDR10_ST2084_EXT
    bool scrgb = false;  // R16G16B16A16_SFLOAT with EXTENDED_SRGB_LINEAR_EXT
};

inline Output choose_output(bool setting_on, const SurfaceOffers& offers) {
    if (!setting_on) return Output::Sdr;
    if (offers.hdr10) return Output::Hdr10;
    if (offers.scrgb) return Output::ScRgb;
    return Output::Sdr;
}

// The levels to use. A setting of 0 means "ask the system"; a system answer of 0 means it could not tell.
inline Levels resolve_levels(double paper_setting, double peak_setting, double system_paper, double system_peak) {
    Levels lv;
    lv.paper_white_nits = paper_setting > 0 ? paper_setting : (system_paper > 0 ? system_paper : 200.0);
    lv.peak_nits = peak_setting > 0 ? peak_setting : (system_peak > 0 ? system_peak : 1000.0);
    lv.peak_nits = std::max(lv.peak_nits, lv.paper_white_nits);  // the shoulder has no room below the paper white
    return lv;
}

}  // namespace hdr_guide
