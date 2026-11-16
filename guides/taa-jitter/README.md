# TAA jitter at a higher internal resolution

**Status:** sections 2 to 5 describe what we did and measured in our own port. The fix is part of our main line.

A game made for one fixed size moves its camera by a fraction of a pixel every frame, so that its temporal anti-aliasing
(TAA) gathers samples from different places inside each pixel over several frames. That movement is the jitter. When a
port draws the 3D scene at a higher internal resolution, the game still jitters by a fraction of *its own* pixel, which
now covers several of the scene's pixels, and the TAA blurs instead of smoothing edges. This guide describes how we
scaled the game's jitter to the internal resolution, which frames we left alone, and how we checked that the picture
improved above the game's size and stayed the same at it. It builds on [Internal and output resolution](../resolution)
and is meant for other people porting a game the same way. You do not need any private code or game data to follow it:
the reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, the scene at the game's 1280x720 and at 3840x2160.
**Not tested: AMD, Intel, Linux.** Nothing here claims to work there.

## What has not been verified

- **The game's own dynamic resolution, in a 3D scene.** We made the game lower its resolution, and at every step the
  jitter stayed within half a pixel of the scene (section 3). Those frames were on the language screen; in a 3D scene
  we checked only that the table stayed as written.
- **An internal resolution other than the output.** The fix takes the internal scale by design, but in every run we
  measured, the scene was drawn at the output's size.
- **The game writing its table again.** The check for it (section 4) never fired in our runs. It is tested by the unit
  tests here only.
- **A scene below the game's size.** We leave the game's jitter as it is there, and did not test it.
- **AMD, Intel, Linux.**

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or measured by a program
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

A scale below is relative to the game's own pixels, as in the resolution guide: the game's 1280x720 drawn at 3840x2160
is a scale of 3.

## 1. The short version

1. Find the unit of the game's jitter. If it is the game's own pixel, a scene at s times the game's size jitters s times
   too far. [section 2]
2. Divide the game's offsets by the internal scale, not the output's: one unit of offset should be one pixel of the
   scene as it is rendered. [section 3]
3. Take the scale from the target each frame actually draws into, so that it follows the game's dynamic resolution.
   [section 3]
4. Change only the game's own TAA. Leave an upscaler's jitter (DLSS, FSR) as it is, and frames with no TAA. [section 3]
5. Keep the game's own values and always scale from them. Check each frame that the game's table still holds what you
   wrote. [section 4]
6. At the game's own size, write nothing. [section 4]
7. Measure still edges, still flicker and a camera turn against frames with no anti-aliasing, and judge "unchanged"
   against the same build run twice. [section 5]

## 2. Why the jitter has to follow the internal resolution

A TAA draws each frame with the camera moved by a small offset taken from a sequence, and blends the frame into a
history of earlier frames. Over the sequence, each pixel is sampled at different places inside its own area, and the
history settles toward how much of the pixel each surface covers: smooth edges in a still picture.

The public guides of two temporal upscalers say which pixel the offset is measured in. NVIDIA's DLSS guide keeps the
offset within half a pixel each way and gives it in pixels of the size rendered, not of the output. AMD's FSR 2
documentation turns its offset into a projection offset by dividing it by the render width and height. [read]

A game made for one size gives its offsets in its own pixels, and ours does. Drawn at a scale s, half a game pixel is
s/2 of the scene's pixels: at 3840x2160 the camera moves up to one and a half of the scene's pixels each way from the
centre, instead of half a pixel. The samples then land in neighbouring pixels instead of inside each pixel, and the
history blends copies of the picture shifted by more than a pixel: a blur. A TAA that checks its history against the
current frame cannot hold such a blend still, so edges stay stepped and a still picture flickers. [inferred]

That is what we measured at 3840x2160 with the game's own jitter: still edges barely smoother than with anti-aliasing
off, and clearly more flicker in still frames. [measured, section 5]

[`tests/sim_jitter_edge.cpp`](tests/sim_jitter_edge.cpp) shows the blur outside any game. With an ideal history, every
frame weighted alike and nothing clamped, offsets in the scene's pixels come close to each pixel's true coverage at any
scale, while offsets in the game's pixels spread an edge over about as many pixels as the scale. A real TAA weights
recent frames more and clamps its history, so it does worse than this, not better. [inferred]

## 3. The fix: scale the offsets, for the game's TAA only

The reference code is [`src/taa_jitter.hpp`](src/taa_jitter.hpp).

- **Divide by the internal scale.** An offset in the game's pixels, divided by s, is the same offset in the scene's
  pixels (`rescale_offset`). If the game stores a position inside the pixel, from 0 to 1, scale its distance from the
  centre instead (`rescale_position`). Ours stores positions.
- **Internal, not output.** The jitter belongs to the scene, so s is the scene's scale. With the scene at 1920x1080
  shown at 3840x2160, s is 1.5, not 3. In our port the TAA itself runs at the internal size: the frame is split after
  its resolve (section 5 of the resolution guide).
- **Per frame, from the target drawn into.** `frame_scale` takes the scale from the target the frame draws its scene
  into, from its size as the game made it and as the port made it. Our game lowers its resolution by making smaller
  targets, and our port makes each of them at the same internal scale (section 7 of the resolution guide). So the scale
  is the same at every step, and our fix uses that one internal scale. A port that changes its own scale from frame to
  frame needs the value of each frame. In a run where we made the game's GPU timer read higher, so that it lowered its
  resolution, it stepped down to its smallest size; at every size the jitter stayed within half a pixel of the scene
  each way, and the table was written once and never needed writing again. [measured] Those frames were on the
  language screen, where the game's TAA runs but no 3D scene is drawn; see the top.
- **Only the game's own TAA, and only above its size** (`rescale_applies`):
  - With a temporal upscaler on, the upscaler's own jitter is used for that frame, and it is already in the pixels it
    renders at (section 2). Scaling it again would make it s times too small. [inferred] Our fix changes only the
    game's TAA path, and a run with DLAA logged the same jitter as before the fix. [measured]
  - With anti-aliasing off, or a spatial one such as SMAA, there is no jitter to fix.
  - At the game's own size the game's values are already right. Below it, we leave them as the game has them.
- **Keep a switch for the old behaviour in the same build.** Our comparisons of the game's jitter against the fix used
  one build and an environment switch, so nothing else differed.

## 4. Writing the game's table

Our game takes its offsets from a table in its own memory. We rewrite that table in place, rather than change the
projection afterwards, so that everything the game derives from the offsets follows together: its projection, and
whatever its TAA uses to undo the jitter. [inferred] The reference is `JitterTable`.

- **Keep the game's own values, and write from them.** The first time, keep a copy and write the rescaled values from
  it. When the scale changes, write from the copy again. Never scale what is in the table: it may be scaled already.
- **Once per scale.** After writing, leave the table alone while it still holds what you wrote.
- **Leave it to an upscaler.** When an upscaler puts its own jitter into the table, forget what you wrote. Coming back
  to the game's TAA, write from the copy again. In our runs we switched live from TAA to SMAA, to off and back, and
  through DLAA: every return to TAA started from the game's own values. [measured]
- **Check every frame.** The game may write its table again, for a new view or a load. Our fix compares the value the
  game will read next with what it wrote, and on a difference compares them all. Keep anew only the values that changed:
  a value the game left still holds the rescaled one, and keeping that would scale it twice, then write it back rescaled
  at the game's size. Across a regression run at 3840x2160, through several scenes and loads, the table was written once
  and the check never fired. [measured]
- **At the game's own size, write nothing.** If something was written, put the game's values back. In our runs at
  1280x720 the port never wrote to the table. [measured]

## 5. How we measured it

Each comparison used runs that differ in one setting, with frames saved at the same moments after the same save loads,
against a reference run with anti-aliasing, motion blur and depth of field off. Three measures:

- **Staircase on still edges.** Find near-horizontal edges column by column, and place each to a fraction of a pixel
  where the brightness crosses the middle of its two sides. Link the places into lines, cut them into pieces 24 pixels
  long, and fit a straight line to each piece. The RMS distance of the places from the line is the staircase: about 0.29
  pixels for an edge drawn without coverage, near 0 for a smooth one. Do the same on the transposed picture for
  near-vertical edges.
- **Flicker in still frames.** With the camera still, the mean change in brightness between consecutive frames. What
  changes is the idle motion of the characters, and, with a TAA, how far its jitter moves the picture.
- **Trails while the camera turns.** In small tiles, fit each run's difference from the reference frame as a share of
  the reference's change back to the frame before, plus a share of its change on to the frame after. Tiles where the
  frame before shows through clearly more than the frame after count as trails left by a history.

Results at 3840x2160, with the scene drawn at that size: [measured]

- **In a field scene, the game's jitter against the fix, one build:**
  - Still edges: with the game's jitter, about as stepped as with anti-aliasing off; with the fix, clearly smoother.
  - Still flicker: with the game's jitter, clearly above anti-aliasing off; the fix took away most of that extra
    flicker, though not all of it.
  - Camera turn: the fix left fewer trail tiles than the game's jitter, still more than anti-aliasing off.
  - With the game's dynamic resolution allowed, much the same as the fix. The game did not lower its resolution in that
    run.
- **Across our regression's checkpoints, the build before the fix against the fix:** every checkpoint with a 3D scene
  had smoother still edges. The title and language screens did not change, although the TAA runs there too: its jitter
  does not show on them.

At the game's own size the fix should change nothing, and "nothing" needs a baseline. Scenes with people walking about
are not reproducible bit for bit, not even from one build run twice. So we judged 1280x720 in three parts: [measured]

- Frames that one build reproduces bit for bit, in our runs the language screen and some title frames, were bit for bit
  the same before and after the fix.
- Elsewhere, the difference between the two builds was about the size of the difference between two runs of one build,
  and it sat on moving people and objects, not on still edges.
- The fix logged no write to the table.

**What the fix does not change.** When the camera turns behind a character who stands still, the game's TAA can leave a
faint outline of the character over the background for a moment. The fix does not remove it: our measure found it with
the game's jitter and with the fix alike, if anything slightly stronger with the fix, by a margin only a little above
the measure's noise. [measured] We did not see it with DLAA, or with DLSS frame generation on, where the game's own TAA
still runs. [seen] We take it to come from the TAA's history, not from the jitter. [inferred]

## 6. What this guide does not cover

- **Upscalers' own jitter.** Their sequences are their own: AMD's FSR 2, for one, makes its sequence longer as the
  upscale ratio grows. [read] This guide leaves them as they are.
- **The game's sequence.** We keep its order and its length; only the size of each offset changes.
- **The TAA's history.** How the game weights and clamps it is unchanged, and so is the outline above.
- **A scene below the game's size, and AMD, Intel, Linux.** Not tested; see the top of this guide.

## 7. Build and run the tests

Any C++17 compiler works; there are no dependencies.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_taa_jitter.cpp -o test_taa_jitter && ./test_taa_jitter
g++ -std=c++17 -Isrc tests/sim_jitter_edge.cpp -o sim_jitter_edge && ./sim_jitter_edge
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code.
The fix came from our own measurements; these documents state the unit of the jitter that it follows:

- NVIDIA, *DLSS Super Resolution Programming Guide*, revision 310.6.0 (March 2026), section 3.7.3, "Required Jitter
  Information":
  [DLSS_Programming_Guide_Release.pdf](https://github.com/NVIDIA/DLSS/blob/d1bef2006b41eefd9d44b0a05f123993f3acbf3c/doc/DLSS_Programming_Guide_Release.pdf)
- AMD, *FidelityFX Super Resolution 2* README, "Camera jitter":
  [README.md#camera-jitter](https://github.com/GPUOpen-Effects/FidelityFX-FSR2/blob/1680d1edd5c034f88ebbbb793d8b88f8842cf804/README.md#camera-jitter)
