#version 450
// A highlight pass, drawn right after the game's own tone map, over its output in a float target. Reference code for
// guides/hdr/README.md. Compiled with glslc, but not drawn by this repository, and grade() below is a stand-in: replace it with
// your game's grading. Pixels the tone map did not clip are discarded, so they keep exactly what the game drew.
// SPDX-License-Identifier: MIT
layout(set = 0, binding = 0) uniform sampler2D scene;  // linear, before the tone map
layout(push_constant) uniform Tone {
    float exposure;
} tone;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

const float kBelowAt = 0.9;
const float kStoreC = 0.5, kStoreK = 1.055 / 2.4;
const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722);

// Stand-in. The real one does what the game's tone map does to a value in 0..1 (for many games, a look-up in a grading LUT) and
// returns its gamma-encoded result.
vec3 grade(vec3 exposed01) {
    vec3 l = exposed01 * (1.0 + exposed01) / 2.0;
    return mix(1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, l * 12.92, lessThanEqual(l, vec3(0.0031308)));
}

vec3 to_linear(vec3 v) { return mix(pow((v + 0.055) / 1.055, vec3(2.4)), v / 12.92, lessThanEqual(v, vec3(0.04045))); }

vec3 store_encode(vec3 l) {
    vec3 sdr = mix(1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, l * 12.92, lessThanEqual(l, vec3(0.0031308)));
    vec3 above = 1.0 + kStoreC * log(1.0 + kStoreK * (l - 1.0) / kStoreC);
    return mix(sdr, above, greaterThan(l, vec3(1.0)));
}

void main() {
    vec3 exposed = max(texture(scene, uv).rgb * tone.exposure, vec3(0.0));
    float m = max(exposed.r, max(exposed.g, exposed.b));
    if (!(m > 1.0)) discard;  // within the SDR range, or not a number: as the game drew it

    // The hue: the colour scaled to 1 in its largest channel, through the game's own grading.
    vec3 at_white = to_linear(grade(exposed / m));

    // The brightness: rising out of white as steeply as the grading does just below it, so that there is no visible edge.
    float white = dot(to_linear(grade(vec3(1.0))), kLuma);
    float below = dot(to_linear(grade(vec3(kBelowAt))), kLuma);
    float slope = clamp((white - below) / ((1.0 - kBelowAt) * max(white, 0.0001)), 0.05, 1.5);

    color = vec4(store_encode(at_white * (1.0 + slope * (m - 1.0))), 1.0);
}
