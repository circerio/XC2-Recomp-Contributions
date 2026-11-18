// Surround output and positioning for a game that mixes for front left and right only. Reference code for
// guides/surround/README.md. Decisions and arithmetic only: no audio API and no game, so all of it can be tested.
// Angles are in degrees: 0 straight ahead, positive to the right, -180 .. 180.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstdint>

namespace surround_guide {

// ---- The output's speakers -----------------------------------------------------------------------------------------

// Speaker bits of a channel mask, as in WAVEFORMATEXTENSIBLE (Windows' SPEAKER_* values).
constexpr uint32_t kFL = 0x1, kFR = 0x2, kFC = 0x4, kLFE = 0x8, kBL = 0x10, kBR = 0x20, kSL = 0x200, kSR = 0x400;

// The layouts this guide outputs. Windows has two 5.1 masks, each with one surround pair: one names it "back", the
// other "side". The name is not where the speakers stand (section 4 of the guide).
constexpr uint32_t kStereo = kFL | kFR;                           // 0x3
constexpr uint32_t k51Back = kFL | kFR | kFC | kLFE | kBL | kBR;  // 0x3F
constexpr uint32_t k51Side = kFL | kFR | kFC | kLFE | kSL | kSR;  // 0x60F
constexpr uint32_t k71 = k51Side | kBL | kBR;                     // 0x63F

inline int channel_count(uint32_t mask) {
    int n = 0;
    for (; mask; mask &= mask - 1) n++;
    return n;
}

// A speaker's channel in the stream: the mask's bits in order, lowest first. -1 when the mask has no such speaker.
// In 7.1 the back pair (0x10, 0x20) comes before the side pair (0x200, 0x400), although the sides stand further forward.
inline int channel_of(uint32_t mask, uint32_t bit) { return (mask & bit) ? channel_count(mask & (bit - 1)) : -1; }

// ---- Which output ----------------------------------------------------------------------------------------------------

enum class Asked { automatic, stereo, five_one, seven_one };

// The mask to output for what the player asked, on a device that reports `device` as its speakers. Automatic follows
// the device: 7.1 when it has all of 7.1, else 5.1 (the side pair first, then the back pair), else stereo. A layout the
// device lacks gives stereo and sets `unsupported`, so the menu can say why. This picks the output only: it never turns
// the positioning on.
inline uint32_t choose_mask(Asked asked, uint32_t device, bool* unsupported) {
    auto has = [&](uint32_t want) { return (device & want) == want; };
    const uint32_t five = has(k51Side) ? k51Side : has(k51Back) ? k51Back : 0;
    *unsupported = false;
    switch (asked) {
    case Asked::automatic: return has(k71) ? k71 : five ? five : kStereo;
    case Asked::stereo: return kStereo;
    case Asked::five_one:
        if (five) return five;
        break;
    case Asked::seven_one:
        if (has(k71)) return k71;
        break;
    }
    *unsupported = true;
    return kStereo;
}

// ---- Where the mix goes ------------------------------------------------------------------------------------------------

// One frame of the mix: the game's six (front pair, centre, LFE, its surround pair) and the positioning's back pair.
struct Frame {
    float fl = 0, fr = 0, fc = 0, lfe = 0, sur_l = 0, sur_r = 0, back_l = 0, back_r = 0;
};

// Adds one frame to `out`, channel_count(mask) values in the mask's order.
// - Stereo: the fold the port had before it had surround, unchanged (front + 0.707 x (centre + surround), the LFE
//   dropped). Keep whatever your own stereo path did. The back pair is dropped: the positioning is off at stereo.
// - 5.1: the game's surround pair on the mask's one pair, side or back. The back pair is dropped: unused at 5.1.
// - 7.1: the game's surround pair on the side pair, the back pair on the back pair.
inline void route(const Frame& f, uint32_t mask, float* out) {
    if (mask == kStereo) {
        out[0] += f.fl + (f.fc + f.sur_l) * 0.707f;
        out[1] += f.fr + (f.fc + f.sur_r) * 0.707f;
        return;
    }
    const bool side = mask & kSL;
    out[channel_of(mask, kFL)] += f.fl;
    out[channel_of(mask, kFR)] += f.fr;
    out[channel_of(mask, kFC)] += f.fc;
    out[channel_of(mask, kLFE)] += f.lfe;
    out[channel_of(mask, side ? kSL : kBL)] += f.sur_l;
    out[channel_of(mask, side ? kSR : kBR)] += f.sur_r;
    if (side && (mask & kBL)) {
        out[channel_of(mask, kBL)] += f.back_l;
        out[channel_of(mask, kBR)] += f.back_r;
    }
}

// ---- Placing a sound ---------------------------------------------------------------------------------------------------

// The speakers a placed sound can reach. Never the centre or the LFE.
enum class Spk { fl, fr, sur_l, sur_r, back_l, back_r };
struct Speaker {
    float az;
    Spk spk;
};
// A circle of speakers in order of angle, -180 .. 180.
struct Ring {
    int count;
    Speaker sp[6];
};

// 5.1: the fronts at +-30 and the surround pair at +-110 (ITU-R BS.775: 100 to 120; ITU-R BS.2051's 5.1 layout: 110).
// The same for both of Windows' 5.1 masks.
constexpr Ring kRing51 = {4, {{-110, Spk::sur_l}, {-30, Spk::fl}, {30, Spk::fr}, {110, Spk::sur_r}}};
// 7.1: the fronts at +-30, the sides at +-90, the backs at +-135 (ITU-R BS.2051's 7.1 layout). Straight behind is on the
// back pair.
constexpr Ring kRing71 = {
    6, {{-135, Spk::back_l}, {-90, Spk::sur_l}, {-30, Spk::fl}, {30, Spk::fr}, {90, Spk::sur_r}, {135, Spk::back_r}}};

// The circle for an output. `back_pair`: the port can feed a back pair. A 7.1 output without one uses the 5.1 circle on
// its front and side speakers, and straight behind falls between the sides. Stereo: none, nothing is placed.
inline const Ring* ring_for(uint32_t mask, bool back_pair) {
    if (mask == k71 && back_pair) return &kRing71;
    return mask == kStereo ? nullptr : &kRing51;
}

constexpr double kPi = 3.14159265358979323846;

// A sound's angle from its direction relative to the listener: `right` positive to the right, `front` positive ahead,
// of any length. False when the direction is not a number or zero.
inline bool azimuth(float right, float front, float* az) {
    if (!std::isfinite(right) || !std::isfinite(front) || (right == 0 && front == 0)) return false;
    *az = float(std::atan2(double(right), double(front)) * 180.0 / kPi);
    return true;
}

// The game's own level for a sound it pans to two front speakers: the power of its two gains.
inline float amplitude_of(float left, float right) { return float(std::sqrt(double(left) * left + double(right) * right)); }

// Equal power between the two neighbouring speakers around `az`: cos and sin of how far along, times 90 degrees, so the
// squares of the gains always sum to amplitude squared. Within a millionth of the way of a speaker, that speaker alone,
// exactly. gains[k] for ring speaker k, all others 0.
inline void spread(float az, float amplitude, const Ring& r, float* gains) {
    for (int k = 0; k < r.count; k++) gains[k] = 0;
    int a = r.count - 1, b = 0;  // the neighbours, going right; the last pair wraps round through 180
    for (int k = 0; k + 1 < r.count; k++)
        if (az >= r.sp[k].az && az < r.sp[k + 1].az) a = k, b = k + 1;
    double from = r.sp[a].az, to = r.sp[b].az, x = az;
    if (b == 0) {
        to += 360;
        if (x < from) x += 360;
    }
    double t = (x - from) / (to - from);
    if (t > 1 - 1e-6) a = b, t = 0;
    if (t < 1e-6) t = 0;
    gains[a] = float(amplitude * std::cos(t * kPi / 2));
    if (t > 0) gains[b] = float(amplitude * std::sin(t * kPi / 2));
}

// One sound as the game hands it to the mixer.
struct Sound {
    bool mono = false;        // one channel
    bool positioned = false;  // the game places it in the world
    float left = 0, right = 0;               // the game's own gains to the front pair
    float dir_right = 0, dir_front = 0;      // the direction the game computed for it, relative to the listener
};
// The gains one sound sends into the mix. No centre and no LFE: nothing placed ever reaches them.
struct Gains {
    float fl = 0, fr = 0, sur_l = 0, sur_r = 0, back_l = 0, back_r = 0;
};

// What one sound sends. Positioning off, a stereo output, a sound that is not a positioned mono sound, or a direction
// that is not one: the game's own two gains, bit for bit, and zero on every other speaker (so a mixer that keeps a
// sound's gains until they are set again is left with nothing on the surrounds after the positioning goes off).
inline Gains place(const Sound& s, bool on, uint32_t mask, bool back_pair) {
    Gains g;
    const Ring* r = ring_for(mask, back_pair);
    float az = 0;
    if (!on || !r || !s.mono || !s.positioned || !azimuth(s.dir_right, s.dir_front, &az)) {
        g.fl = s.left, g.fr = s.right;
        return g;
    }
    float k[6];
    spread(az, amplitude_of(s.left, s.right), *r, k);
    for (int i = 0; i < r->count; i++) {
        switch (r->sp[i].spk) {
        case Spk::fl: g.fl = k[i]; break;
        case Spk::fr: g.fr = k[i]; break;
        case Spk::sur_l: g.sur_l = k[i]; break;
        case Spk::sur_r: g.sur_r = k[i]; break;
        case Spk::back_l: g.back_l = k[i]; break;
        case Spk::back_r: g.back_r = k[i]; break;
        }
    }
    return g;
}

}  // namespace surround_guide
