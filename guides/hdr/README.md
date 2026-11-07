# HDR output from a game made for SDR

This describes how we output HDR10 and scRGB from a native Vulkan port of a game that was made for SDR: how to get a correct picture onto an HDR
swapchain, what to do about peak brightness and paper white, and how to keep highlights the game would have clipped, with their hue. You do not
need any private code or game data to follow it: the reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, and one OLED monitor whose HDR mode reports a 550-nit peak, with Windows HDR on and its SDR content
brightness at 360 nits.

Each result says how we know it:

- **[seen]** a person looked at it on the HDR monitor
- **[measured]** logged by the program, or computed from dumped frames
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

## What has not been verified

| Situation | What we know |
|---|---|
| **AMD and Intel GPUs** | Nothing has been run on them. |
| **HDR with exclusive fullscreen** | It starts, gets an HDR10 swapchain and runs with no errors. [measured] **Nobody has checked the picture.** |
| **HDR with frame generation** | Not tested. |
| **HDR with an upscaler** | Not checked as a combination. |
| **The roll-off in this guide** | The reference code compresses on the largest channel. Our main line still compresses each channel on its own, so neither this roll-off, its lower brightness in saturated colours, nor the blend described in section 3 has been seen in a game by us (section 5). |
| **Very bright scenes** | We never identified the sun's disc in a frame, only the brightest direction. In an open daytime sky the picture sits close to the peak, and nobody has judged how that looks. |
| **Monitors other than this one** | A person has only looked at a 550-nit peak. A 1000-nit peak was only run in headless tests, read back as numbers. We don't know whether the reported peak changes when a monitor changes HDR mode. |
| **Other screens** | The highlight pass was checked in a dusk scene and in daytime scenes. We have not confirmed that menus, cutscenes, night scenes and interiors go through the same tone map. |
| **The right answer** | The console has no HDR output, so there is no reference to compare with. What highlights should look like is a choice, not a measurement. |

## 1. The short version

1. Do it in two stages. First get the SDR picture onto an HDR swapchain correctly. Only then add highlights. The first stage can be checked with numbers.
2. Decode with the piecewise sRGB curve, the one Windows uses for SDR content, and scale so that SDR white is the paper white.
3. Take the default peak from the display and the default paper white from Windows. A fixed paper white made our HDR look dimmer than SDR. [seen]
4. If the game has a linear scene before its tone map, use that. Don't try to reconstruct highlights from the SDR result.
5. Let the game's own tone map run as it is, and draw the clipped pixels again with your own pass. You then ship no modified game shader.
6. Scale a clipped colour to 1 in its largest channel to keep its hue. Clipping each channel turns bright colours yellow and then white.
7. Near the peak, decide how to compress. Compressing each channel moves the hue toward white and keeps more brightness. Compressing on the largest channel keeps the hue and gives up brightness in saturated colours. You can also blend the two (section 3).
8. If the post-processing chain works on gamma-encoded values, store highlights with a curve that grows slowly, so that later passes don't meet huge numbers.

## 2. Stage 1: the SDR picture on an HDR swapchain

### Getting the swapchain

Enable `VK_EXT_swapchain_colorspace` on the instance. **Without it the surface only reports `SRGB_NONLINEAR` formats**, which looks as if the display had no HDR. [measured] Then take the first of these the
surface offers (`choose_output` in [`src/hdr_math.hpp`](src/hdr_math.hpp)):

| Output | Format | Colour space |
|---|---|---|
| HDR10 | `VK_FORMAT_A2B10G10R10_UNORM_PACK32` | `VK_COLOR_SPACE_HDR10_ST2084_EXT` |
| scRGB | `VK_FORMAT_R16G16B16A16_SFLOAT` | `VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT` |

If neither is offered, stay in SDR and say why in the log. With the setting off, nothing here should run, and that is easy to check with your normal regression.

If the device has `VK_EXT_hdr_metadata`, call `vkSetHdrMetadataEXT` once the swapchain exists. We set the primaries of the output's colour space (BT.2020 for HDR10, BT.709 for scRGB), a D65 white point, `maxLuminance` and
`maxContentLightLevel` to the peak, `minLuminance` to 0, and `maxFrameAverageLightLevel` to the paper white.

A plain blit can't encode anything, so the HDR output has to go through a full-screen draw. If you use a swapchain proxy such as an upscaler's or a frame generator's, make sure the colour space reaches it; that combination is one of the things we have not verified.

### The present pass

[`src/present_hdr.frag`](src/present_hdr.frag) does this for each pixel:

1. Decode to linear light, with SDR white at 1, using the piecewise sRGB curve.
2. Apply the shoulder from section 3.
3. Multiply by the paper white to get nits.
4. **HDR10:** convert BT.709 to BT.2020, then encode with PQ (ST 2084). **scRGB:** divide by 80, since 1.0 is 80 nits. It stays in BT.709 primaries.

With only this, the picture is the SDR picture at paper white brightness: no highlights yet, and the correct place to start.

## 3. Peak brightness and paper white

**Paper white** is how bright SDR white is. **Peak** is the brightest value you send.

| Level | Default | Fallback | How to read it on Windows |
|---|---|---|---|
| Peak | What the display reports | 1000 nits | DXGI `IDXGIOutput6::GetDesc1`, `MaxLuminance`, for the output showing the window |
| Paper white | What Windows uses for SDR content | 200 nits | `DisplayConfigGetDeviceInfo` with `DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL`; 1000 in that API means 80 nits |

[`src/windows_display_levels.hpp`](src/windows_display_levels.hpp) has both. They aren't per-frame calls, so ask when you create a swapchain. Offer fixed values as settings too; we allow 400 to 10000 nits for the peak and 80 to 500 for the paper white, and 0 means "ask the system".

**Why the paper white should follow Windows.** Our first default was a fixed 200 nits. Windows was showing SDR at 360 nits, so turning HDR on made the whole picture 1.8 times dimmer, and SDR looked more glaring than HDR. [seen] That comparison was unfair, and a player would make the same one. Following Windows means turning HDR on doesn't change the brightness of the SDR range.

**The cost.** Paper white and peak share the same room. At 360 and 550 nits, highlights have only 1.53 times the brightness of SDR white. In an open daytime sky most of that was used at once: one sky region had a median of about 530 nits. [measured] Someone who wants more room for highlights can set a lower fixed paper white, so keep that setting.

**The shoulder.** A hard clip at the peak flattens everything above it. Instead, with `m` the largest channel, above SDR white, and `P` the peak over the paper white:

```
out = colour * (1 + (P - 1) * (1 - exp(-(m - 1) / (P - 1)))) / m
```

Its slope at SDR white is 1, the peak is its limit, nothing changes while every channel is at or below SDR white, and the ratios between channels stay the same (`roll_off` in the reference code).

**Each channel or the largest channel: a trade-off.** The reference code uses the largest channel, but that is not a finding that it looks better: nobody has compared the two on a screen.

Compressing each channel on its own squeezes the largest ones most, so bright colours drift toward yellow and white. The less room there is, the sooner it happens. This is what happens to a colour whose R:G starts at 2.00 (arithmetic, checked in [`tests/test_hdr_math.cpp`](tests/test_hdr_math.cpp)):

| Largest channel before the shoulder | R:G, each channel, paper white 360 / peak 550 | R:G, each channel, paper white 200 / peak 1000 | R:G, largest channel |
|---|---|---|---|
| 1.5 times SDR white | 1.76 | 1.96 | 2.00 |
| 3 times SDR white | 1.15 | 1.75 | 2.00 |
| 6 times SDR white | 1.01 | 1.50 | 2.00 |

The largest channel keeps the hue, but it costs brightness in saturated colours. It scales the other channels down with the largest one, so green, which carries most of the luminance, stops rising when the largest channel does. Part of the brightness you get from compressing each channel comes from the hue moving toward white.

**The table below is calculated on example colours, not measured from game frames.** Inputs are linear light where SDR white is 1. Values are luminance in nits with BT.709 weights (also checked in the tests):

| Example colour (R, G, B) | Paper white 360 / peak 550: each channel → largest channel | Paper white 200 / peak 1000: each channel → largest channel |
|---|---|---|
| Orange (3, 1.5, 0.375) | 466 → 316 (32% less) | 325 → 298 (8% less) |
| The same orange, twice as bright (6, 3, 0.75) | 527 → 319 (40% less) | 543 → 446 (18% less) |
| Bright blue (1.2, 1.6, 3) | 478 → 294 (39% less) | 311 → 277 (11% less) |
| Blue (0.6, 0.8, 1.5) | 286 → 257 (10% less) | 161 → 158 (2% less) |
| Pale, nearly white (2.4, 2.5, 2.8) | 539 → 486 (10% less) | 450 → 437 (3% less) |
| White (3, 3, 3) | 546, unchanged | 515, unchanged |

For a given hue, luminance still rises with exposure, but at 360 and 550 nits this orange stops near 319 nits, below the paper white. Saturated colours can look brighter than their luminance suggests, so how much of this is visible has to be judged on a screen. [inferred]

**A middle way: blend the two results by a strength.** With `t` from 0 to 1:

```
out = (1 - t) * largest_channel_result + t * each_channel_result
```

`t = 0` is the largest channel and `t = 1` is each channel. Both results stay within the peak, so the blend does too. At `t = 0.5`, the orange at (3, 1.5, 0.375) with 360 and 550 nits comes to about 391 nits and an R:G of 1.46: both between the two methods. The reference code does not include this, and we have not tried it in a game.

We took the idea of a strength from the `hdr_hue` setting in [PR #103 of xcx-recomp](https://github.com/jmars125/xcx-recomp/pull/103) by jmars125 (0 to 100, 50 by default). What it blends is different from the formula above: above SDR white and before its roll-off, it moves the hue-kept colour toward the game's own SDR result at the same luminance, fading in as the largest channel goes from 1 to 2.

We learned the largest-channel roll-off from reading the same PR. Our first version, and the first version of this guide, compressed each channel, and **our main line still does**: see section 5.

## 4. Stage 2: keeping highlights

### Where they are lost

Look at your game's last passes. Ours has a linear HDR scene with bloom added, then a tone map that multiplies by exposure, **clips to 0..1**, and looks the result up in grading LUTs that write an 8-bit target. After that come a sharpen pass and the interface, both working on gamma-encoded values.

So the highlights exist in the scene and are lost in two places: the clip, and the 8-bit targets after it.

### What we chose not to do

- **Reconstructing highlights from the SDR result**, as Windows Auto HDR does. The game has the linear scene, so use it.
- **Editing the game's tone map shader.** Our first plan was to translate it back to GLSL and change it. That would make a modified copy of a game shader part of our source, which we don't want to keep or ship. A separate pass has none of that, and it holds only our own code.

### Each frame, with HDR on

1. **The game's tone map runs as it is**, but its target is replaced by an `RGBA16F` stand-in. Its result in the SDR range stays what it was.
2. **A highlight pass draws right after it** ([`src/highlights.frag`](src/highlights.frag)). It reads the scene and reads the exposure from the game's own constants at run time. It discards every pixel whose exposed value is at most 1 in all channels, so those keep what the game drew. The check is written so that a NaN is discarded as well.
3. **The sharpen pass and the interface run as usual**, on `RGBA16F` stand-ins of their targets. They still hold gamma-encoded values, so blending behaves as the game expects. Opaque interface elements stay at or below 1, which is the paper white.
4. **The present pass decodes the picture** and applies the shoulder.
5. **The stand-ins are retired after the present.** The original targets get their values clipped to 0..1, so anything that reads them later sees what it always did.

### The hue

For a pixel whose largest exposed channel `m` is above 1:

```
colour at white = grade(exposed / m)        the same hue, with its largest channel at 1, through the game's own grading
brightness      = 1 + s * (m - 1)
```

Scaling keeps the ratios between channels. Clipping each channel to 1, which is what happens in SDR, moves `(4, 2, 0.5)` to `(1, 1, 0.5)`: an orange becomes yellow, and then white as the last channel clips. The test in [`tests/test_hdr_math.cpp`](tests/test_hdr_math.cpp) shows this.

Going through the game's grading keeps its colour style at the top of the range. If your grading mixes LUTs per pixel by something awkward to recompute, you can recover the mix from what the game actually drew at the clipped value, with a least-squares fit along the difference between the two LUT results. That's what we did.

### No edge where a highlight starts

`s` is how steeply the game's grading rises just below white, relative to its value at white: sample the grading's grey curve at 1 and at 0.9 (`top_slope`). A pixel just above white then continues the slope of a pixel just below it. In our dumps the brightness distribution was continuous around SDR white, with no gap. [measured]

### Storing values above 1

Later passes were written for values in 0..1. A sharpen filter such as `center * a - neighbours * b` meeting a linear value of 20 next to it can cut a black frame around a bright point. So above SDR white we store:

```
v = 1 + c * ln(1 + k * (L - 1) / c)        c = 0.5, k = 1.055 / 2.4
```

`k` is the slope of the sRGB curve at 1, so the curve goes on from sRGB with no kink, and a very bright value stays within about 1 to 3. It's invertible, and the present pass undoes it (`store_encode`, `store_decode`).

## 5. The choice: hue or white

> **Our main line still compresses each channel on its own. The picture with the roll-off in this guide has not been verified: nobody has looked at it.** Everything we describe seeing below comes from the main line, so it is not what the reference code in this guide draws. Near the peak, compressing each channel pushes colours toward yellow and white (section 3), so some of what follows, such as a lamp's yellow, may come from that and not from keeping the hue. It also keeps more brightness in saturated colours than the reference code does, so the same scenes may be less bright with the reference code. [inferred]

Keeping the hue means a lamp's core stays bright yellow instead of going white, and a daytime sky stays blue instead of washing out. Rolling highlights toward white with brightness ("path to white") is the other option, and closer to how the SDR picture looks.

We chose the hue. What that looked like in our main line, where the highlight pass keeps the ratios between channels and the shoulder then moves them near the peak: [seen, dusk scene; dumps, daytime]

- Lamp faces have a clear edge and their frames are visible, where SDR shows a white core with a soft yellow bloom over the frame. The values come from the game's own scene, so this isn't a rendering error, but it is different from the game's SDR style.
- A daytime sky that SDR clips to a large white area keeps its blue and the layers of its clouds. It is much bluer than the SDR picture.
- The brightest direction reaches the peak with clearer edges, where SDR is a white blob that eats the edges of nearby objects.

There's no reference to compare with, so test both on your game and pick one. The same goes for how the shoulder compresses (section 3).

## 6. Checking it with numbers

The check that worked compares three runs of the same moment: SDR, HDR10 and scRGB. Dump the swapchain image, decode it back to nits, and for stage 1:

1. **Bias.** Among pixels that are the same picture (within 8 of 255 in each channel), take the mean difference between HDR and SDR, by SDR brightness band and by channel. A wrong curve, matrix or paper white shows up here.
2. **Content.** The share of pixels within 2 of 255, against the two HDR outputs compared with each other as a control.
3. **Range.** The maximum is at most the peak, and the median of pixels that are black in SDR is about 0 nits.

**What did not work:** our first version limited the 99.9th percentile of the difference over the whole frame, and it failed by a wide margin. The frames were not identical between runs: a swinging sign, walking characters and flickering lamps differ as much between the two HDR runs as between HDR and SDR. A whole-frame limit measures the scene, not the encoding. Use a control pair.

Results for stage 1, over 6 frames in each output. Nothing is above SDR white in stage 1, so they do not depend on how the shoulder compresses: [measured]

| Check | HDR10 | scRGB |
|---|---|---|
| Worst bias, in levels of 255 | -0.20 to -0.44, just below white; within 0.1 elsewhere | within 0.23 |
| Maximum at a 200-nit paper white | 201.9 nits | 200.0 nits |
| Black, median | 0 nits | 0 nits |

The HDR10 bias near white is about half a 10-bit PQ code step at that brightness, so it's the format's precision and not a mistake.

**These limits only apply to stage 1.** Stage 2 changes some pixels on purpose, and our bias and content checks were not updated to leave those pixels out, so they fail on stage 2 as expected. For stage 2 we looked at how many pixels rose above the paper white and how far. These numbers come from our main line, with each channel compressed on its own, and we have not measured them again with the largest-channel roll-off: [measured]

| Scene | Levels | Pixels above paper white | Brightest |
|---|---|---|---|
| Dusk | Paper white 200, peak 1000 | 0.10 to 0.79% | 670 to 845 nits |
| Daytime, looking at the sky | Paper white 360, peak 550 | 7 to 10% | 555 nits |
| Daytime, facing the open side | Paper white 360, peak 550 | Up to 32% | 555 nits |

**A side effect in the SDR range.** With the intermediate targets in float, two 8-bit rounding steps are gone, and the sharpen pass amplifies the difference. Pixels within 1 of 255 of the stage 1 picture fell to about 41%, while those within 2 stayed at 90 to 97%, about what two separate runs differ by. We don't expect this to be visible, but it means the SDR range is no longer bit-identical. [inferred]

## 7. Known limits

- **The highlight pass recognises one tone map.** A mod that changes the post-processing chain so that it no longer runs should lose stage 2, and HDR should fall back to stage 1 without highlights. We saw that tone map disappear under such a mod, but did not run HDR with it. [inferred]
- **Some sharp-edged blocks in a daytime sky** sat at 1.3 to 1.45 times SDR white, away from where the pass starts. They look like the clouds' own texture, but we did not check the scene image directly. [inferred]
- **The reference shaders are compiled but not drawn by this repository.** They use the same formulas as `hdr_math.hpp`, which is what the tests check.

## 8. Build and run the tests

Any C++17 compiler works. The checks of the arithmetic have no dependencies. If `glslc` is on your PATH, the shaders are compiled as tests too.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

On Windows this also builds `print_display_levels`, which prints what the system reports for your primary monitor.

## Sources and related work

The code here was written for this guide and is under this repository's MIT licence. It contains no third-party code. Referenced: SMPTE ST 2084 for PQ, ITU-R BT.2087 for the primaries' conversion, the Vulkan
specification (`VK_EXT_swapchain_colorspace`, `VK_EXT_hdr_metadata`), and Microsoft's DXGI and DisplayConfig documentation.

**Rolling off on the largest channel and blending by a strength (section 3) are ideas we learned from jmars125's [PR #103 of xcx-recomp](https://github.com/jmars125/xcx-recomp/pull/103).** That PR's tone map credits its approach to [TotK Recomp](https://github.com/valentderah/totk-pc-recomp)'s, and through it to [RenoDX](https://github.com/clshortfuse/renodx)'s TotK mod by souperman9 and Musa; its notices name correcting the hue of highlights toward the game's SDR result as one of the ideas from that mod. We did not trace where the roll-off itself first came from. We took the principles only and copied no code: the curve is the shoulder we already had, which starts at SDR white, where the PR's starts at half the peak when that is higher. We have not compared the two.

Monado's notes recommend the same direction as this guide: an HDR10 swapchain, a tone map that follows the game's, and the interface at paper white. We used no code from it.
