// Internal and output resolution for a game that renders at one fixed size: the 3D scene at an internal scale, the
// interface at the output scale. Reference code for guides/resolution/README.md. Decisions only: no GPU calls, so all of
// it can be unit tested. A scale is relative to the game's own pixels (the game's 1280x720 shown at 3840x2160 is 3).
//
// The shader side (render_scale values for translated shaders) follows Ryujinx's resolution scaling; see the guide's
// sources.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace resolution_guide {

struct Size {
    uint32_t w = 0, h = 0;
};

inline uint32_t scaled(uint32_t v, float s) { return std::max<uint32_t>(1, uint32_t(std::lround(double(v) * s))); }

// ---- Which images get which scale ----------------------------------------------------------------------------------

enum class ScaleClass { other, scene, window };

struct ImageInfo {
    Size size;
    bool is_2d = true;
    bool multisampled = false;
    bool render_target = false;  // made to be drawn into (or first used as an attachment)
    bool presented = false;      // one of the textures the game presents (its window)
};

// The game's full frame, or one of its dynamic-resolution sizes: same aspect within 2%, more than half the game's height
// and at most all of it. A game that lowers its resolution by making smaller targets (not a smaller viewport) needs this.
inline bool screen_sized(Size image, Size game) {
    if (!image.w || !image.h || image.w > game.w || image.h > game.h || 2 * image.h <= game.h) return false;
    const double want = double(game.w) / game.h, have = double(image.w) / image.h;
    return std::fabs(have - want) <= 0.02 * want;
}

// Presented textures take the output scale; screen-sized 2D render targets the internal scale; everything else stays
// at the game's size: half-size effects, shadow maps, lookup tables, interface art, textures the CPU uploads.
inline ScaleClass classify(const ImageInfo& i, Size game) {
    if (!i.is_2d || i.multisampled) return ScaleClass::other;
    if (i.presented) return ScaleClass::window;
    if (i.render_target && screen_sized(i.size, game)) return ScaleClass::scene;
    return ScaleClass::other;
}

inline float scale_of(ScaleClass c, float internal, float output) {
    return c == ScaleClass::scene ? internal : c == ScaleClass::window ? output : 1.0f;
}

// ---- Commands given in the game's pixels ---------------------------------------------------------------------------

struct Viewport {
    float x, y, w, h;
};

struct Rect {
    int32_t x, y;
    uint32_t w, h;
};

inline Viewport scale_viewport(Viewport v, float s) { return {v.x * s, v.y * s, v.w * s, v.h * s}; }

// A scissor scaled to the smallest whole-pixel rectangle that covers it, kept inside the pass.
inline Rect scale_scissor(Rect r, float s, Size pass) {
    const double x0 = std::floor(r.x * double(s)), y0 = std::floor(r.y * double(s));
    const double x1 = std::ceil((r.x + double(r.w)) * s), y1 = std::ceil((r.y + double(r.h)) * s);
    const double cx0 = std::clamp(x0, 0.0, double(pass.w)), cy0 = std::clamp(y0, 0.0, double(pass.h));
    const double cx1 = std::clamp(x1, 0.0, double(pass.w)), cy1 = std::clamp(y1, 0.0, double(pass.h));
    return {int32_t(cx0), int32_t(cy0), uint32_t(cx1 - cx0), uint32_t(cy1 - cy0)};
}

// A copy between two images: a plain copy when they have the same scale, else a scaled blit, linear for colour that
// can be filtered and nearest for anything else (depth, integer formats, packed values).
enum class CopyKind { copy, blit_linear, blit_nearest };

inline CopyKind copy_kind(float src_scale, float dst_scale, bool filterable_colour) {
    if (src_scale == dst_scale) return CopyKind::copy;
    return filterable_colour ? CopyKind::blit_linear : CopyKind::blit_nearest;
}

// ---- What a translated shader is told ------------------------------------------------------------------------------

enum class Stage { vertex, fragment, geometry, compute };

// The scale a shader stage gets for one bound texture, or nothing when it keeps the default of 1. The translated shader
// multiplies texel-fetch coordinates by it and divides size queries by it, so the shader keeps working in the game's
// pixels. A negative value tells a fragment stage to fetch by its own pixel position (gl_FragCoord) instead: used when
// the texture has the target's scale and proportions, so each pixel reads its own texel, not the one at the game's
// coarser position. A geometry stage gets no rescaling code from the translator, so it cannot be told anything.
inline std::optional<float> texture_scale_value(Stage stage, float texture_scale, Size texture_game, float target_scale,
                                                Size target_game) {
    if (texture_scale == 1.0f || stage == Stage::geometry) return std::nullopt;
    const bool same_proportion = texture_game.w && texture_game.h && target_game.w && target_game.h &&
                                 uint64_t(target_game.w) * texture_game.h == uint64_t(target_game.h) * texture_game.w;
    if (stage == Stage::fragment && texture_scale == target_scale && same_proportion) return -texture_scale;
    return texture_scale;
}

// ---- One pass, attachments at two scales ---------------------------------------------------------------------------

struct Attachment {
    float scale;
    ScaleClass cls;
    bool used_by_scene_pass;  // has this image ever been a target of a scene pass?
};

// promote: make the image again at the larger scale, for good, keeping its content (scaled). For an image the scene
// never draws into, such as a depth buffer only the interface uses. twin: draw into a larger copy of a scene image for
// the rest of the frame (see Twins). unresolved: neither applies; log it.
enum class Fix { none, promote, twin, unresolved };

inline std::vector<Fix> fix_pass(const std::vector<Attachment>& a) {
    float top = 0.0f;
    for (const Attachment& x : a) top = std::max(top, x.scale);
    std::vector<Fix> out;
    for (const Attachment& x : a) {
        if (x.scale == top) out.push_back(Fix::none);
        else if (!x.used_by_scene_pass) out.push_back(Fix::promote);
        else if (x.cls == ScaleClass::scene) out.push_back(Fix::twin);
        else out.push_back(Fix::unresolved);
    }
    return out;
}

// ---- The scene and the interface apart -----------------------------------------------------------------------------

// After the split point (the draw that resolves the game's temporal anti-aliasing, the last draw that needs the scene's
// own pixels), any draw into a scene image goes to that image's twin at the output scale instead: tone mapping,
// sharpening and the interface then run at the output size. A twin is seeded with what the scene left in the image,
// scaled up; this is also where an upscaler's output goes. At the present the twins retire; their content is scaled
// back into the scene images before the next frame's work, so a later frame that reads one sees what it expects.
class Twins {
public:
    // Before each draw, for its target (an image id, and whether it is a scene image): the image to draw into, the target
    // itself or its twin (id | kTwinBit). `seeded` is set when the twin starts here, so its seed must be drawn first.
    static constexpr uint64_t kTwinBit = 1ull << 63;

    uint64_t target(uint64_t image, bool scene_image, bool& seeded) {
        seeded = false;
        if (!split_ || !scene_image) return image;
        if (std::find(live_.begin(), live_.end(), image) == live_.end()) {
            live_.push_back(image);
            seeded = true;
        }
        return image | kTwinBit;
    }

    // After the draw that resolves the anti-aliasing.
    void split() { split_ = true; }

    // At the present: the images whose twins retire, to be scaled back before the next frame's first draw.
    std::vector<uint64_t> present() {
        std::vector<uint64_t> fold = std::move(live_);
        live_.clear();
        split_ = false;
        return fold;
    }

    // A texture read during the frame resolves to the twin while that is live.
    uint64_t read(uint64_t image) const {
        return std::find(live_.begin(), live_.end(), image) != live_.end() ? (image | kTwinBit) : image;
    }

private:
    bool split_ = false;
    std::vector<uint64_t> live_;
};

// ---- Distance-field text at the output scale -----------------------------------------------------------------------

// A text shader that draws a distance-field glyph's edge between two field values (say 0.4 to 0.6) makes that soft edge
// a fixed width in the field's units. Drawn at s times the size, the edge is s times wider on screen. Keep the centre
// (the stroke weight) and divide the half width by min(s, cap). With no cap, very thin strokes whose field barely
// passes the centre start to break up; a cap of 2 was the balance for us.
struct EdgeWindow {
    float lo, hi;
};

inline EdgeWindow narrow_edge(EdgeWindow w, float output_scale, float cap = 2.0f) {
    const float div = std::clamp(output_scale, 1.0f, cap);
    if (div == 1.0f) return w;  // the game's own size: exactly the game's values
    const float centre = 0.5f * (w.lo + w.hi), half = 0.5f * (w.hi - w.lo) / div;
    return {centre - half, centre + half};
}

}  // namespace resolution_guide
