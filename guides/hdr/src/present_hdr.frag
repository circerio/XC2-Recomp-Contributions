#version 450
// The last pass before an HDR swapchain: a full-screen draw of the finished picture (the game's output, with the interface on it).
// Reference code for guides/hdr/README.md, the same formulas as src/hdr_math.hpp. Compiled with glslc, but not drawn by this
// repository. The picture holds gamma-encoded values: 0..1 is the SDR range, and above 1 are highlights, stored as in
// store_encode. Without a highlight pass nothing is above 1, and this alone gives correct SDR content on an HDR display.
// SPDX-License-Identifier: MIT
layout(set = 0, binding = 0) uniform sampler2D picture;
layout(push_constant) uniform Levels {
    float output_kind;  // 1: HDR10, 2: scRGB
    float paper_white_nits;
    float peak_nits;
} levels;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

const float kStoreC = 0.5, kStoreK = 1.055 / 2.4;

vec3 to_linear(vec3 v) {  // SDR white is 1
    vec3 sdr = mix(pow((v + 0.055) / 1.055, vec3(2.4)), v / 12.92, lessThanEqual(v, vec3(0.04045)));
    vec3 above = 1.0 + (kStoreC / kStoreK) * (exp((v - 1.0) / kStoreC) - 1.0);
    return mix(sdr, above, greaterThan(v, vec3(1.0)));
}

// The shoulder on the largest channel, every channel scaled alike, so that the hue does not change near the peak.
vec3 roll_off(vec3 l, float top) {
    float m = max(l.r, max(l.g, l.b));
    if (m <= 1.0) return l;
    float room = max(top - 1.0, 0.001);
    return l * (1.0 + room * (1.0 - exp(-(m - 1.0) / room))) / m;
}

vec3 pq_encode(vec3 nits) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

void main() {
    vec3 linear = to_linear(max(texture(picture, uv).rgb, vec3(0.0)));
    vec3 nits = roll_off(linear, levels.peak_nits / levels.paper_white_nits) * levels.paper_white_nits;
    if (levels.output_kind < 1.5) {
        const mat3 bt709_to_bt2020 = mat3(0.6274040, 0.0690970, 0.0163916,  // by column
                                          0.3292820, 0.9195400, 0.0880132,
                                          0.0433136, 0.0113612, 0.8955950);
        color = vec4(pq_encode(bt709_to_bt2020 * nits), 1.0);
    } else {
        color = vec4(nits / 80.0, 1.0);
    }
}
