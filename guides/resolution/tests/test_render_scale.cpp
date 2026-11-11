// The rules of render_scale.hpp.
// SPDX-License-Identifier: MIT
#include "render_scale.hpp"

#include <cmath>
#include <cstdio>

using namespace resolution_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static const Size kGame{1280, 720};

static ImageInfo target(uint32_t w, uint32_t h) {
    ImageInfo i;
    i.size = {w, h};
    i.render_target = true;
    return i;
}

int main() {
    {  // Which images are scaled.
        CHECK(classify(target(1280, 720), kGame) == ScaleClass::scene);
        CHECK(classify(target(896, 504), kGame) == ScaleClass::scene);     // a dynamic-resolution step
        CHECK(classify(target(640, 360), kGame) == ScaleClass::other);     // half size: an effect, kept
        CHECK(classify(target(1024, 1024), kGame) == ScaleClass::other);   // a shadow map
        CHECK(classify(target(1920, 1080), kGame) == ScaleClass::other);   // larger than the game's frame
        ImageInfo uploaded = target(1280, 720);
        uploaded.render_target = false;                                     // a texture the CPU fills
        CHECK(classify(uploaded, kGame) == ScaleClass::other);
        ImageInfo msaa = target(1280, 720);
        msaa.multisampled = true;
        CHECK(classify(msaa, kGame) == ScaleClass::other);
        ImageInfo window = target(1280, 720);
        window.presented = true;
        CHECK(classify(window, kGame) == ScaleClass::window);
        CHECK(scale_of(ScaleClass::scene, 2.0f, 3.0f) == 2.0f);
        CHECK(scale_of(ScaleClass::window, 2.0f, 3.0f) == 3.0f);
        CHECK(scale_of(ScaleClass::other, 2.0f, 3.0f) == 1.0f);
    }
    {  // Viewports scale exactly; scissors cover at least what they covered, inside the pass.
        const Viewport v = scale_viewport({10, 20, 640, 360}, 1.5f);
        CHECK(v.x == 15 && v.y == 30 && v.w == 960 && v.h == 540);
        const Rect r = scale_scissor({1, 1, 3, 3}, 1.5f, {1920, 1080});
        CHECK(r.x == 1 && r.y == 1 && r.w == 5 && r.h == 5);  // 1.5..6.0 covered by 1..6
        const Rect edge = scale_scissor({1270, 710, 20, 20}, 3.0f, {3840, 2160});
        CHECK(edge.x == 3810 && edge.y == 2130 && edge.w == 30 && edge.h == 30);  // clamped to the pass
        CHECK(scaled(1280, 1.5f) == 1920 && scaled(1, 0.25f) == 1);
    }
    {  // Copies.
        CHECK(copy_kind(2.0f, 2.0f, true) == CopyKind::copy);
        CHECK(copy_kind(1.0f, 3.0f, true) == CopyKind::blit_linear);
        CHECK(copy_kind(3.0f, 1.0f, false) == CopyKind::blit_nearest);
    }
    {  // The shader's texture scales.
        // Unscaled textures keep the default.
        CHECK(!texture_scale_value(Stage::fragment, 1.0f, {640, 360}, 2.0f, kGame));
        // A screen-sized texture read by a pass at the same scale: each pixel fetches its own texel.
        CHECK(*texture_scale_value(Stage::fragment, 2.0f, kGame, 2.0f, kGame) == -2.0f);
        // The same texture read by a pass at another scale, or by a vertex stage: plain scale.
        CHECK(*texture_scale_value(Stage::fragment, 2.0f, kGame, 3.0f, kGame) == 2.0f);
        CHECK(*texture_scale_value(Stage::vertex, 2.0f, kGame, 2.0f, kGame) == 2.0f);
        // A texture of other proportions at the target's scale: plain scale.
        CHECK(*texture_scale_value(Stage::fragment, 2.0f, {1280, 640}, 2.0f, kGame) == 2.0f);
        // A dynamic-resolution step read into the full frame: same proportions, so by pixel position.
        CHECK(*texture_scale_value(Stage::fragment, 2.0f, {896, 504}, 2.0f, kGame) == -2.0f);
        // Nothing reaches a geometry stage.
        CHECK(!texture_scale_value(Stage::geometry, 2.0f, kGame, 2.0f, kGame));
    }
    {  // One pass, two scales.
        // The interface draws into the presented texture (output 3) with a depth buffer the scene never used (1).
        auto f = fix_pass({{3.0f, ScaleClass::window, true}, {1.0f, ScaleClass::other, false}});
        CHECK(f[0] == Fix::none && f[1] == Fix::promote);
        // After the split, a scene image (internal 2) drawn with the presented texture (3): a twin.
        f = fix_pass({{2.0f, ScaleClass::scene, true}, {3.0f, ScaleClass::window, true}});
        CHECK(f[0] == Fix::twin && f[1] == Fix::none);
        // A smaller image that is neither: say so instead of guessing.
        f = fix_pass({{3.0f, ScaleClass::window, true}, {1.0f, ScaleClass::other, true}});
        CHECK(f[1] == Fix::unresolved);
        // All at one scale: nothing to do.
        f = fix_pass({{2.0f, ScaleClass::scene, true}, {2.0f, ScaleClass::scene, true}});
        CHECK(f[0] == Fix::none && f[1] == Fix::none);
    }
    {  // Twins over two frames.
        Twins t;
        bool seeded = false;
        CHECK(t.target(7, true, seeded) == 7 && !seeded);           // before the split: the scene image itself
        t.split();
        CHECK(t.target(7, true, seeded) == (7 | Twins::kTwinBit) && seeded);   // first draw after it: seed the twin
        CHECK(t.target(7, true, seeded) == (7 | Twins::kTwinBit) && !seeded);  // later draws: already seeded
        CHECK(t.target(9, false, seeded) == 9);                     // not a scene image: untouched
        CHECK(t.read(7) == (7 | Twins::kTwinBit) && t.read(8) == 8);
        const std::vector<uint64_t> fold = t.present();
        CHECK(fold.size() == 1 && fold[0] == 7);                    // scaled back before the next frame
        CHECK(t.read(7) == 7);
        CHECK(t.target(7, true, seeded) == 7 && !seeded);           // the next frame starts before its split again
    }
    {  // Distance-field text: centre kept, half width divided by min(scale, 2).
        const EdgeWindow game{0.4f, 0.6f};
        const EdgeWindow at4k = narrow_edge(game, 3.0f);
        CHECK(std::fabs(at4k.lo - 0.45f) < 1e-6f && std::fabs(at4k.hi - 0.55f) < 1e-6f);
        const EdgeWindow at1440 = narrow_edge(game, 2.0f);
        CHECK(std::fabs(at1440.lo - 0.45f) < 1e-6f && std::fabs(at1440.hi - 0.55f) < 1e-6f);
        const EdgeWindow at1080 = narrow_edge(game, 1.5f);
        CHECK(std::fabs((at1080.hi - at1080.lo) - 0.2f / 1.5f) < 1e-6f);
        const EdgeWindow at720 = narrow_edge(game, 1.0f);
        CHECK(at720.lo == game.lo && at720.hi == game.hi);           // the game's own size: unchanged
        const EdgeWindow uncapped = narrow_edge(game, 3.0f, 3.0f);
        CHECK(std::fabs((uncapped.hi - uncapped.lo) - 0.2f / 3.0f) < 1e-6f);
    }

    if (failures == 0) std::printf("all render scale checks passed\n");
    return failures == 0 ? 0 : 1;
}
