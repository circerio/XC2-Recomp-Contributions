// The rules of taa_jitter.hpp.
// SPDX-License-Identifier: MIT
#include "taa_jitter.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace taa_jitter_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// A made-up table of positions inside the pixel (x and y interleaved), not any game's.
static std::vector<float> game_table() { return {0.5f, 0.25f, 0.75f, 0.5f, 0.125f, 0.875f, 0.625f, 0.375f}; }

static bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static bool all_rescaled(const std::vector<float>& t, const std::vector<float>& from, float s) {
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] != rescale_position(from[i], s)) return false;
    return true;
}

int main() {
    {  // The arithmetic: the centre stays, the offset shrinks by the scale.
        for (float s : {1.5f, 2.0f, 3.0f}) {
            CHECK(near(rescale_position(0.5f, s), 0.5f));
            CHECK(near(rescale_offset(0.0f, s), 0.0f));
            for (float p : {0.0f, 0.1f, 0.3333f, 0.75f, 0.999f}) {
                CHECK(near((rescale_position(p, s) - 0.5f) * s, p - 0.5f));  // the same offset in scene pixels
                CHECK(near(rescale_offset(p - 0.5f, s) * s, p - 0.5f));
            }
        }
        // At 3 (1280x720 drawn at 3840x2160): half a game pixel each way becomes half a scene pixel each way.
        CHECK(near(rescale_position(0.0f, 3.0f), 0.5f - 1.0f / 6.0f));
        CHECK(near(rescale_position(1.0f, 3.0f), 0.5f + 1.0f / 6.0f));
        CHECK(near(rescale(-0.5f, 3.0f, Form::offset), -1.0f / 6.0f));
    }
    {  // The scale comes from the frame's own target, internal, not output.
        CHECK(near(frame_scale({1280, 720}, {3840, 2160}), 3.0f));
        CHECK(near(frame_scale({1280, 720}, {1920, 1080}), 1.5f));
        CHECK(near(frame_scale({1280, 720}, {1280, 720}), 1.0f));
        // A dynamic-resolution step the game made smaller, which the port made at the same internal scale: still 3.
        CHECK(near(frame_scale({896, 504}, {2688, 1512}), 3.0f));
        // An odd internal scale: rounding of the height does not matter.
        CHECK(std::fabs(frame_scale({896, 504}, {1512, 851}) - 1.6875f) < 1e-4f);
        // Not one scale, or no size: leave the jitter alone.
        CHECK(frame_scale({1280, 720}, {3840, 1080}) == 0.0f);
        CHECK(frame_scale({0, 0}, {3840, 2160}) == 0.0f);
    }
    {  // Which frames.
        CHECK(rescale_applies(AaPath::game_taa, 3.0f));
        CHECK(rescale_applies(AaPath::game_taa, 1.5f));
        CHECK(!rescale_applies(AaPath::game_taa, 1.0f));   // the game's size: its values are right
        CHECK(!rescale_applies(AaPath::game_taa, 0.5f));   // below it: left as the game has it
        CHECK(!rescale_applies(AaPath::game_taa, 0.0f));   // no common scale
        CHECK(!rescale_applies(AaPath::upscaler, 3.0f));   // DLSS / FSR bring their own jitter
        CHECK(!rescale_applies(AaPath::other, 3.0f));      // no TAA, no jitter
    }
    {  // At the game's size the table is never touched.
        const std::vector<float> orig = game_table();
        std::vector<float> t = orig;
        JitterTable j(Form::position);
        for (int f = 0; f < 3; f++)
            CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::nothing);
        CHECK(same_bits(t, orig));
        CHECK(j.holds() == JitterTable::Holds::game);
    }
    {  // Above it: written once per scale, always from the game's own values.
        const std::vector<float> orig = game_table();
        std::vector<float> t = orig;
        JitterTable j(Form::position);
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::wrote);
        CHECK(all_rescaled(t, orig, 3.0f));
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::nothing);  // still holds it
        CHECK(all_rescaled(t, orig, 3.0f));
        // A new internal scale: from the game's values, not from the rescaled ones (that would scale twice).
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 2.0f) == JitterTable::Did::wrote);
        CHECK(all_rescaled(t, orig, 2.0f));
        CHECK(j.written_scale() == 2.0f);
        // An upscaler takes the table over and writes its own jitter into it.
        CHECK(j.frame(t.data(), t.size(), AaPath::upscaler, 2.0f) == JitterTable::Did::left_to_upscaler);
        t[0] = 0.9f, t[1] = 0.1f;
        // Back to the game's TAA: written from the game's values again, the upscaler's left behind.
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 2.0f) == JitterTable::Did::wrote);
        CHECK(all_rescaled(t, orig, 2.0f));
        // Anti-aliasing off, or back to the game's own size: the game's values back, bit for bit.
        CHECK(j.frame(t.data(), t.size(), AaPath::other, 2.0f) == JitterTable::Did::restored);
        CHECK(same_bits(t, orig));
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::wrote);
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::restored);
        CHECK(same_bits(t, orig));
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::nothing);
    }
    {  // The upscaler from the very first frame: the game's values are still kept for later.
        const std::vector<float> orig = game_table();
        std::vector<float> t = orig;
        JitterTable j(Form::position);
        CHECK(j.frame(t.data(), t.size(), AaPath::upscaler, 3.0f) == JitterTable::Did::left_to_upscaler);
        t[2] = 0.3f;  // the upscaler's
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::wrote);
        CHECK(all_rescaled(t, orig, 3.0f));
    }
    {  // The game writes its whole table again while it holds rescaled values: kept anew, written rescaled.
        const std::vector<float> orig = game_table();
        std::vector<float> t = orig;
        JitterTable j(Form::position);
        j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f);
        const std::vector<float> fresh = {0.4f, 0.6f, 0.2f, 0.8f, 0.7f, 0.3f, 0.9f, 0.1f};
        t = fresh;
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::kept_anew);
        CHECK(j.kept_anew_last_frame() == fresh.size());
        CHECK(all_rescaled(t, fresh, 3.0f));
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::restored);
        CHECK(same_bits(t, fresh));
    }
    {  // The game writes two values again: only those are kept anew. The others keep the game's true values.
        const std::vector<float> orig = game_table();
        std::vector<float> t = orig;
        JitterTable j(Form::position);
        j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f);
        t[2] = 0.2f, t[5] = 0.6f;
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 3.0f) == JitterTable::Did::kept_anew);
        CHECK(j.kept_anew_last_frame() == 2);
        std::vector<float> want = orig;
        want[2] = 0.2f, want[5] = 0.6f;
        CHECK(all_rescaled(t, want, 3.0f));
        // Back at the game's size, every value is the game's own: none of them still rescaled.
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::restored);
        CHECK(same_bits(t, want));
    }
    {  // A table of offsets from the centre instead of positions.
        std::vector<float> t = {-0.5f, 0.25f, 0.0f, -0.125f};
        const std::vector<float> orig = t;
        JitterTable j(Form::offset);
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 2.0f) == JitterTable::Did::wrote);
        for (size_t i = 0; i < t.size(); i++) CHECK(t[i] == orig[i] / 2.0f);
        CHECK(j.frame(t.data(), t.size(), AaPath::game_taa, 1.0f) == JitterTable::Did::restored);
        CHECK(same_bits(t, orig));
    }

    if (failures == 0) std::printf("all TAA jitter checks passed\n");
    return failures == 0 ? 0 : 1;
}
