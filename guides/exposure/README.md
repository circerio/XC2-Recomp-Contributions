# Auto exposure at any frame rate

**Status:** sections 2 to 6 describe what we did and measured in our own port. The fix is part of our main line.

A game made for a fixed 30 fps usually adapts its exposure, the eye adaptation you see when the camera goes from a dark
place to a bright one, by a fixed amount each frame. Raise the frame rate and it adapts faster: at 60 fps in half the time.
This guide is about putting it back on real time. It is meant for other people porting a game the same way. You do not need
any private code or game data to follow it: the reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, frame rate caps of 30 and above.
**Not tested: AMD, Intel, Linux.** The timing in sections 3 to 5 is CPU logic, but section 2 depends on the GPU and we have
only seen it on this one.

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged by the program
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Before touching the timing, check that the brightness measurement reaches the CPU at all. In our port it did not at
   first, and the exposure was stuck. [measured, section 2]
2. Find everything in the exposure loop that moves once per frame: the smoothing toward the measured brightness, and
   anything that advances a slot or a counter. Ours had both. [measured]
3. Put it on real time, either by scaling each step by the frame time or by running the whole per-frame update on a
   30 Hz tick and holding it in between. We chose the tick. [section 4]
4. If GPU results come back a frame or two late, credit each one to the slot it measured, not to "two frames ago".
   Once steps are held, frames and steps no longer line up. [section 5]
5. At the original frame rate, call the original code untouched.
6. Check it with a brightness change you can repeat, at 30 and above, and log the exposure state rather than judging by
   eye. [measured, section 6]

## 2. First: does the measurement reach the CPU?

A common design measures the scene's brightness on the GPU, reduces it to a single pixel, and reads that pixel on the CPU a
frame or two later to steer the exposure. Some designs only average the pixels inside a brightness range, and find the
range with a histogram whose bins are counted by occlusion queries.

On a console the CPU can read a render target straight from memory. In a PC port the render target is a GPU image, and
nothing copies it back unless you do. What we found in ours: [measured]

- **The CPU read memory the GPU never wrote.** The game saw an invalid value and stopped adapting. The exposure then came
  only from the scene's fixed limits, so a bright scene and a dark one ended up looking the same.
- **The occlusion queries returned a made-up count.** Ours answered with a number that was never zero, so that nothing
  would be culled. The histogram came out flat and its range stuck, the reduction found no pixels inside it, and the
  GPU's own measurement was zero as well.

Both had to be fixed before anything else made sense:

- Copy the one-pixel target back to where the CPU reads it once the GPU has finished with it. We added no wait for this:
  the game already uses a measurement a frame or two old and smooths it. [measured]
- Give the queries real sample counts. [measured]

Real query counts changed more than the exposure. Anything else the game decides with occlusion queries started to
work as designed too, so expect visible changes besides brightness and check them. [measured]

## 3. Why a higher frame rate adapts faster

The usual step, in log space, once per frame:

```
log(adapted) += k * (log(measured) - log(adapted))
```

After n frames, (1 - k)^n of the gap is left. At 60 fps there are twice as many frames in a second, so the gap closes in half
the time. The same goes for anything else that moves once per frame: a histogram that counts one bin per frame cycles twice
as fast.

In our port, at caps above 30, the histogram's bins per second followed the frame rate, and the picture settled after a
camera turn much sooner than at 30. [measured]

[`tests/sim_exposure.cpp`](tests/sim_exposure.cpp) shows this on a synthetic step in brightness, with a loop tuned for 30 fps:

```
time to cover 90% of a brightness step, seconds (loop tuned for 30 fps)
frame rate      per frame   scaled step   30 Hz tick
        30         0.733         0.733        0.733
        60         0.367         0.733        0.733
       120         0.183         0.733        0.733
       144         0.153         0.729        0.736
       165         0.133         0.733        0.733
       240         0.092         0.729        0.733
```

It proves nothing about a real game; it shows the mechanism and that both fixes in the next section hold the time.

## 4. Two ways to put it back on real time

The reference code is [`src/exposure_timing.hpp`](src/exposure_timing.hpp).

### Scale each step by the frame time

`adapt_step_dt` replaces k with 1 - (1 - k)^(dt / base_dt), where base_dt is the frame time k was tuned for. For a
brightness that holds still this is exact: any split of the same time into frames ends in the same place.

It fixes the smoothing only. Anything else in the loop that advances per frame still runs fast and needs its own change,
and some of it cannot be scaled at all: a histogram slot is a whole number.

### Run the per-frame update on a 30 Hz tick

`Tick` adds each frame's time; the frame that completes a tick runs the original update, and the frames in between hold
it. Holding means no smoothing step and every piece of per-frame state put back as it was, so the render side measures
the same slot again. The steps then come in exactly the sequence they do at 30 fps, and nothing inside needs rescaling.

This is what we did. Our loop had more in it than the smoothing: a histogram that counted one bin per frame, and GPU
results that came back two frames later. The tick kept all of it as it was at 30 fps. [measured]

Things to get right:

- **Put back all of the state.** Our first version missed one value that the update recomputes each frame, and the
  histogram's range jumped around. [measured] Find everything the update writes before you hold it.
- **Any frame rate works, not only multiples of 30.** At 60 the k-th step lands at k/30 s, as at 30 fps. At 144 a tick
  falls every 4 or 5 frames, so a step comes at most one frame late; the tests check both.
- **At the original frame rate, call the original code.** We do, so 30 fps behaves exactly as before. `Tick` also has a
  small slack so that rounding does not drop a tick at exactly 30 fps, but the plain call is simpler to trust.
- **A frame longer than a tick gives one tick, not two.** The time is lost, the same way a 30 fps game slows down rather
  than catching up.

The exposure still moves in 30 Hz steps, each shown for a few frames, as it is at 30 fps. [inferred]

## 5. Late GPU results once steps are held

A query or a readback comes back a frame or two after it was recorded. Code written for 30 fps knows which slot a result
belongs to: two frames back is two steps back, because every frame is a step. Hold the steps and that stops being true.
At 60 fps, two frames back is one step back, or none.

`DelayedResults` carries the slot with each result, and the update credits the result to that slot.
`slot_by_frame_offset` is the 30 fps assumption, kept to show the mistake: the test runs both at 30, 60 and 144 fps, and the
offset credits wrong slots as soon as steps are held. [`tests/test_exposure_timing.cpp`](tests/test_exposure_timing.cpp)

In our port we credited each count to the slot it measured, and the histogram's range stayed steady at caps of 60 and 144.
[measured]

## 6. How we checked it

We turned the camera between the same bright view and dark view, logging the game's exposure state every frame: at a
30 fps cap, at higher caps with the fix, and at a high cap with the fix off. [measured]

- **At 30:** about 30 histogram bins a second.
- **With the fix, at caps of 60 and 144:** still about 30 bins a second, and the time to settle after a turn back in the
  same range as at 30.
- **Without the fix, at a cap of 144:** the bins followed the frame rate, and the picture settled much sooner.

The time to settle moved between runs with small changes in the scene, so read it for its size, not as an exact value.

A separate check then measured how fast the picture brightened after a turn: at caps of 60 and 144 it was the same as at 30,
and within the range we measured from a recording of the original console. [measured] The person playing found it
natural, with no snap. [seen]

## 7. What this guide does not cover

- **Other effects that move once per frame.** Motion blur or a fade can hide the same mistake. We listed ours but did not
  measure them.
- **AMD, Intel, Linux.** Not tested; see the top of this guide.
- **Matching the console exactly.** We compared rates of change with a console recording, not the same frames side by side.

## 8. Build and run the tests

Any C++17 compiler works; there are no dependencies.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_exposure_timing.cpp -o test_exposure_timing && ./test_exposure_timing
g++ -std=c++17 -Isrc tests/sim_exposure.cpp -o sim_exposure && ./sim_exposure
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code.
