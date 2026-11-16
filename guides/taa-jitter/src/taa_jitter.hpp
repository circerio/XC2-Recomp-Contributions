// A game's own temporal anti-aliasing (TAA) jitter at a higher internal resolution. Reference code for
// guides/taa-jitter/README.md. Decisions and arithmetic only: no GPU calls and no game, so all of it can be tested.
// A scale is relative to the game's own pixels, as in guides/resolution: the game's 1280x720 drawn at 3840x2160 is 3.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace taa_jitter_guide {

struct Size {
    uint32_t w = 0, h = 0;
};

// ---- How far to scale ----------------------------------------------------------------------------------------------

// The scale of the target a frame draws its scene into: the scene's pixels per game pixel, from that target's size as
// the game made it and as the port made it. Taken per frame, so a game that lowers its resolution by making smaller
// targets, or a port that changes its internal scale, is followed. This is the internal scale, never the output's.
// 0 when the two sizes do not share one scale (a target the port did not scale evenly): leave the jitter alone then.
inline float frame_scale(Size game_target, Size scene_target) {
    if (!game_target.w || !game_target.h || !scene_target.w || !scene_target.h) return 0.0f;
    const double sx = double(scene_target.w) / game_target.w, sy = double(scene_target.h) / game_target.h;
    if (std::fabs(sx - sy) > 0.01 * sx) return 0.0f;  // rounding of odd sizes stays well inside 1 %
    return float(sx);
}

// ---- Which frames --------------------------------------------------------------------------------------------------

// game_taa: the game's own TAA draws this frame. upscaler: a temporal upscaler (DLSS, FSR) does, with its own jitter.
// other: anti-aliasing off, or a spatial one such as SMAA, with no jitter.
enum class AaPath { game_taa, upscaler, other };

// Only the game's own TAA, and only above the game's size. An upscaler's jitter is already in the pixels it renders at;
// at the game's size the game's values are already right; below it we leave them as the game has them.
inline bool rescale_applies(AaPath path, float s) { return path == AaPath::game_taa && s > 1.0f; }

// ---- The arithmetic ------------------------------------------------------------------------------------------------

// An offset from the pixel centre in the game's pixels, as the same offset in the scene's pixels.
inline float rescale_offset(float offset, float s) { return offset / s; }

// The same for a game that stores a position inside the pixel (0 to 1, the centre at 0.5) instead of an offset.
inline float rescale_position(float position, float s) { return 0.5f + (position - 0.5f) / s; }

enum class Form { offset, position };

inline float rescale(float v, float s, Form form) {
    return form == Form::position ? rescale_position(v, s) : rescale_offset(v, s);
}

// ---- The game's table ----------------------------------------------------------------------------------------------

// A jitter table in the game's memory, rewritten in place, so that everything the game derives from it (its projection,
// and anything its TAA un-jitters) follows together. One object per table (per view, if the game keeps several).
//
// - The game's own values are kept, and every write is made from them, never from what the table holds now: scaling a
//   written value again would scale it twice.
// - At the game's size nothing is written. If something was, the game's values go back.
// - A table that another path writes (an upscaler putting its own jitter there) is left to it. Coming back, the next
//   write starts from the kept values again.
// - Each frame the table is checked for values the game wrote again (a new view, a load). Only values that no longer
//   hold what was written are kept anew: one the game left still holds the rescaled value, and keeping that would
//   scale it twice and later write it back rescaled at the game's size.
class JitterTable {
public:
    enum class Holds { game, rescaled, other };  // what the table holds now, as far as this object knows
    enum class Did { nothing, wrote, kept_anew, restored, left_to_upscaler };

    explicit JitterTable(Form form) : form_(form) {}

    // Once a frame, before the game reads the table for that frame. Every value (x and y alike) is scaled the same.
    Did frame(float* table, size_t n, AaPath path, float s) {
        if (kept_.size() != n) {  // first sight of this table, or the game made it anew at another length
            kept_.assign(table, table + n);
            holds_ = Holds::game;
        }
        // 1. Bring the kept values up to date with the table.
        anew_ = 0;
        if (holds_ == Holds::game) kept_.assign(table, table + n);   // the game's own values: keep the latest
        else if (holds_ == Holds::rescaled) anew_ = keep_changed(table, n);
        // Holds::other: the table holds another path's values; the kept copy stands.

        // 2. Write what this frame needs.
        if (path == AaPath::upscaler) {
            holds_ = Holds::other;
            return Did::left_to_upscaler;
        }
        if (rescale_applies(path, s)) {
            if (holds_ == Holds::rescaled && s == s_ && !anew_) return Did::nothing;  // still holds what was written
            for (size_t i = 0; i < n; i++) table[i] = rescale(kept_[i], s, form_);
            holds_ = Holds::rescaled;
            s_ = s;
            return anew_ ? Did::kept_anew : Did::wrote;
        }
        if (holds_ != Holds::game) {  // the game's size again, or no TAA: the game's own values back
            for (size_t i = 0; i < n; i++) table[i] = kept_[i];
            holds_ = Holds::game;
            s_ = 0.0f;
            return Did::restored;
        }
        return Did::nothing;  // never written: not touched at all
    }

    Holds holds() const { return holds_; }
    float written_scale() const { return s_; }
    size_t kept_anew_last_frame() const { return anew_; }

private:
    size_t keep_changed(const float* table, size_t n) {
        size_t changed = 0;
        for (size_t i = 0; i < n; i++)
            if (table[i] != rescale(kept_[i], s_, form_)) kept_[i] = table[i], changed++;
        return changed;
    }

    Form form_;
    Holds holds_ = Holds::game;
    float s_ = 0.0f;
    size_t anew_ = 0;
    std::vector<float> kept_;
};

}  // namespace taa_jitter_guide
