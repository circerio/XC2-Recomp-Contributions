# Super resolution (DLSS, FSR), and anti-aliasing without it

**Status:** sections 2 to 11 describe what is in our main line: DLSS Super Resolution and FSR Super Resolution, both
off by default, and without them the game's own TAA (the default) or none. The anti-aliasing row also offers SMAA,
which is not finished and not covered here.

A game with temporal anti-aliasing (TAA) already makes almost everything a temporal upscaler needs: the scene before
anti-aliasing, its depth, motion vectors and a camera jitter. A static recompilation leaves the game as it is, but it
sees every draw, so an upscaler can take the place of the TAA's picture. This guide describes where we put it, where
each input comes from and in which unit, how we checked that, and what happens when a library is missing or the player
switches. It is meant for other people porting a game the same way. You do not need any private code or game data to
follow it, and it has no code of its own. It builds on [Internal and output resolution](../resolution), [TAA
jitter](../taa-jitter) and [PC settings](../settings).

**Tested on one machine only:** Windows 11, an NVIDIA GPU, output up to 3840x2160.
**Not tested: AMD, Intel, Linux.** FSR ran on the NVIDIA GPU only.

## What has not been verified

- **Which upscaler or mode looks better.** We do not say. Nobody has judged it.
- **Camera cuts.** We do not reset the history on a cut (section 8), and have not looked at what a cut leaves.
- **Battles and scenes full of effects.** We measured one route through one area, and one cutscene.
- **Transparent surfaces and particles** write no motion of their own. We do not pass the game's mask for fog and
  volumetric light by default, and have not compared its effect.
- **The sky's motion vectors** while the camera turns.
- **The texture bias and the sharpening made a copy** (section 6), against doing neither.
- **An upscaler that fails while it runs,** and a settings file that asks for one that cannot start (section 9).
- **Freeing an upscaler's memory** when the display memory is short (section 10).
- **Later builds.** The checks of section 5 were made when the feature went in, and not all repeated. The counter of
  section 4 was repeated.

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or measured by a program
- **[read]** taken from a vendor's public document
- **[inferred]** our reasoning, not proven

"In enlarged stills" means screenshots of scripted runs compared side by side, not a person at the game.

## 1. The short version

1. Put the upscaler right after the TAA resolve and replace only that draw's picture. [section 2]
2. Take every input from that draw's own textures and constants. [section 3]
3. Give the game the upscaler's jitter in the game's unit, and the upscaler the jitter read back from the frame.
   [section 4]
4. Decode the motion vectors, and measure their unit, their direction and whether they carry the jitter. [section 5]
5. Tone map and draw the interface afterwards, at the output size. [section 6]
6. Let the mode set the internal size, hold it fixed, and reset the history when it changes. [sections 7 and 8]
7. If an upscaler is missing or fails, show the game's own picture and say why. [section 9]
8. Start a switch at a frame whose jitter has not been given yet. [section 10]

## 2. Where it goes

After the scene, our game's frame goes on like this: TAA resolve, bloom, tone mapping, sharpening into the presented
image, interface. We recognise the resolve by its fragment shader, not by its place in the frame.

- **The resolve still runs.** It also makes the bloom's source, the packed depth the tone mapping reads, and its
  history. Only its colour output is replaced.
- **The upscaler reads what the resolve reads:** the colour before TAA.
- **It writes into the output-size copy** of the resolve's colour image (section 5 of the resolution guide) instead of
  the scaled-up seed. At the native modes there is no copy, and it writes into the image itself.
- **Before tone mapping.** We hand both upscalers the colour as linear HDR, with each library's HDR flag (DLSS
  `colorBuffersHDR`, FSR `FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE`): it is what the resolve reads, before bloom and the
  tone map. This is the scene's range, not HDR output; for that, see the [HDR guide](../hdr). Our tone mapping also
  reads a packed depth that must not be interpolated, so it is better run after the upscaler. [inferred]

A frame without a resolve is not upscaled, such as our game's options screen. [measured]

## 3. The inputs

| Input | Where it comes from | What we pass |
|---|---|---|
| Colour | The texture the resolve reads, linear HDR | As it is, with the library's HDR flag |
| Depth | The scene depth the resolve reads | As it is, near 0, not reversed. Never the packed copy |
| Motion vectors | The velocity texture the resolve reads | Decoded by a compute pass (section 5) |
| Jitter | The resolve's constants | In pixels of the internal size (section 4) |
| Exposure | Not read from the game | Automatic in both, pre-exposure 1 |
| Camera | The scene's vertex constants | Planes, field of view and matrices where the frame has them, else defaults |

The camera we read matched the game's own position in every sampled frame. Giving DLSS the real matrices instead of
identity matrices changed the picture no more than running one setting twice. [measured]

Keep a third backend that only scales the colour before TAA to the output. It shows whether the inputs and the jitter
arrive.

## 4. Jitter

Three units meet here:

- **The game's table** holds positions inside a pixel of the game's own size, from 0 to 1.
- **The resolve's constants** hold the offset as a share of the game's frame.
- **The upscaler** wants pixels of the internal size, within half a pixel each way.

With s the internal scale:

- **To the game:** the upscaler's offset divided by s, plus one half. We write it into the table at each present, for
  the next frame, and the game puts it into its projection itself, so every pass gets the same jitter. In one captured
  frame the depth pre-pass and the colour pass that tests for equal depth read one projection [measured]; jitter added
  by other means would have to reach both.
- **To the upscaler:** the value from the resolve's constants, times the game's size and s. That is the jitter the
  frame was drawn with, whenever the game read it.
- **The sequence** is Halton (2, 3), its length eight times the square of output over internal size. That is FSR's
  documented length [read], and we use it for DLSS too.

How it was checked: [measured]

- A counter compares the resolve's jitter with the one written for that frame. In eighteen runs of 3,600 frames every
  frame took its own entry. In a run with changes of size, 6 of about 10,800 frames, all around a change, took another
  value; the upscaler still got the jitter they were drawn with.
- With a still camera we tried all four sign combinations. One was stable. With the others, consecutive frames
  differed more than twenty times as much.
- The jitter in the scene's projection matched the resolve's in every frame where both could be read.

Above the game's size its own TAA has a jitter problem of its own: see the TAA jitter guide.

## 5. Motion vectors

In one captured frame the objects of the scene wrote their own velocity, skeletal animation included, and no
full-screen pass wrote it. [measured] It is packed: each axis a square root, its sign in spare bits. A compute pass
decodes it to pixels of the internal size, from this frame's position to the previous one's, without the jitter. DLSS
gets 1/width and 1/height as its scale, FSR 1, and neither is told the vectors contain the jitter.

How it was checked: [measured]

- **Unit.** A pass sums every vector and logs the mean. Divided by the internal width, a scripted camera pan's mean
  was 0.0217 to 0.0219, and the mean length while walking 0.0088 to 0.0090, in all ten modes, two runs each.
- **Direction.** Across two frames of the pan, the shift that fits the whole picture best had the sign of the vectors.
- **No jitter in it.** In still views the mean vector stayed near zero and did not follow the jitter.
- **Objects and background.** With the camera still, only characters had vectors. While it turned, practically every
  pixel had one.
- **After the camera stops.** The change between consecutive frames was back near the still level within a few frames,
  with no slowly fading trail, except in a run where a character walked by.

Do not measure quality by the pixel difference from a reference run: passers-by and idle motion differ each time.

## 6. After the upscaler

- **The post chain and the interface run at the output size.** In enlarged stills, icons and text had the output's
  detail in all ten modes.
- **The interface has no jitter.** In one captured frame none of its draws reads the scene's camera. [measured]
- **The game's sharpening becomes a plain copy**, by changing one constant of its shader. FSR's own sharpening is off
  too.
- **Scene textures get a negative mip bias,** the base-2 logarithm of internal over output size, only for draws into
  the full-size scene targets before the resolve. Both vendors recommend it. [read] Put the bias into the sampler
  cache's key, or a change of mode keeps the old samplers.

## 7. Modes and menu rows

| Row value | DLSS, share of output | FSR, share of output |
|---|---|---|
| Native (DLAA, native AA) | 1 | 1 |
| Quality | 2/3 | 1/1.5 |
| Balanced | 0.58 | 1/1.7 |
| Performance | 1/2 | 1/2 |
| Ultra performance | 1/3 | 1/3 |

The shares are constants in our code, not asked of the library. How rows grey out is in the settings guide; for these:

- **Two rows:** which upscaler (off, DLSS, FSR), and its mode, greyed while it is off.
- **Greyed while one is on:** render resolution, the game's dynamic resolution, and anti-aliasing.
- **Stepped past:** an upscaler that is missing, or of another vendor than the frame generation chosen.
- **At the game's own output size** the hint recommends the native mode, since every other renders below it.
- **FSR version:** on our GPU the library offered 3.1 and 2.3, and we pin 3.1. [measured]

## 8. Reset, and a fixed internal size

The reset flag is set when the frame before cannot be used as history:

- it had no resolve, as in menus and loading,
- the internal size changed, or
- it was drawn for another upscaler or the anti-aliasing option.

In runs with three size changes the log shows four resets: one at the start, one for each change. [measured]

**Not on a camera cut.** A cut changes none of these, so the history carries over. A limit on the camera's turn or
jump could find cuts; our main line does not do it.

Both upscalers get a fixed internal size, so the game's dynamic resolution stays off while one is on (section 7 of the
resolution guide). Answering its GPU timers as if the GPU cost nothing stopped being enough once our replay had its
own thread; we clear the game's own switch at each present. [measured]

## 9. When a library is missing or cannot start

| Situation | What happens |
|---|---|
| Built without the vendor's SDK, or its libraries are not beside the executable | Not offered |
| DLSS's loader is not signed by NVIDIA | Not loaded. FSR's loader is not checked |
| The library says the GPU cannot run it | DLSS is off, and the log says so |
| Chosen while the game runs, but not loaded at this start | It waits for the next start, and the caption says so |
| A frame's inputs are missing, or the call fails | That frame shows the game's TAA picture, scaled up; logged |

- Missing libraries were tested by pointing the search at an empty folder: both rows read "unavailable" with their
  reason. [measured] That was a layout with a row for each upscaler; the current one steps past the value. The other
  rows of the table are untested.
- **Known gap.** If the file asks, at the start, for an upscaler that cannot start, the picture stays the game's TAA,
  but the scene still renders at the mode's smaller size and is scaled up. Found by reading the code, not run.

**Start order.** DLSS's loader must be initialised before the Vulkan instance and be told of the device afterwards.
[read] It also takes over the present and swapchain calls. So that a player can switch later, we start it whenever its
libraries are there. That makes the start longer and uses more memory. [measured]

## 10. Switching while the game runs

Which upscaler runs, its mode and the anti-aliasing option can change while the game plays.

- **A switch starts at a frame whose jitter the game has not been given,** so every frame is upscaled as it was
  jittered, and the sizes change at that frame.
- **One switch at a time.** The first frame of another upscaler or another size resets (section 8).
- **Two threads read the switch:** the game's, for the next frame's jitter, and the one that draws the frame. Writing
  and reading share a lock (section 12).
- **The upscaler no longer used** keeps its memory while the display memory has room, else it is freed.
- **FSR's context takes long to make at a large output.** We make it when the graphics page first opens. The first FSR
  frame after a switch was then far shorter. [measured]

## 11. Without an upscaler

The same place in the frame offers these choices while no upscaler is on:

- **The game's TAA,** the default. Nothing is replaced.
- **Off.** The picture is the colour the resolve reads, drawn without jitter.

With off, the sharpening stays a copy.

At the game's own size, in a still view, about a third of a patch's pixels changed between two frames with the TAA.
With off, up to a twelfth changed, by one step out of 255. [measured]

The row's other choice, SMAA, is not finished, and this guide does not cover it.

## 12. Problems found and fixed

| Problem | How it showed | Fix |
|---|---|---|
| The jitter hook never ran | Its log line never appeared. The function is called directly (settings guide, section 4) | Write the table as data at each present |
| The resolve's jitter was read as clip space, half its size | Still pictures shook more than they should | It is a share of the frame; all earlier picture runs were discarded |
| Wrong frames looked like the upscaler's | Single frames of wild motion vectors, camera still | Data replayed after the game had changed it; fixed elsewhere, all runs repeated |
| A start on a side thread could hang | 6 of 20 starts, after a third-party Vulkan layer faulted in it, waiting for a lock [inferred] | Start on the instance's thread: 0 of 20 |
| A switch could be read half published | By reading the code. A unit test fails without the lock, passes with it. No game run triggered it, with or without | One lock around both |

## 13. How we checked it

By program: [measured]

- All ten modes ran one scripted route and one cutscene. In the logs we read, every frame was upscaled, with no
  failure and no missing input.
- In two consecutive frames of a still view, compared at the game's size, every mode changed less than the game's TAA;
  in a second view, all but one did.
- With the upscalers off, the regression against its reference pictures passed.

The picture itself is closed by these measurements, not by anyone's judgement of it. In play, the mode, the upscaler and
the anti-aliasing were changed while the game ran, and the log shows each switch. [measured] Nobody was asked which looks
better.

## 14. What this guide does not cover

Frame generation and Reflex, which use some of the same inputs, and the vendors' models and presets.

## Sources

There is no code in this folder. These documents state the rules it follows:

- NVIDIA, Streamline SDK, *Programming Guide, Manual Hooking*.
- AMD, FidelityFX SDK 2.4.0, the super resolution upscaler's documentation:
  [FidelityFX-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)
- The checks for jitter in every pass, velocity per object and the interface outside the upscaler follow a list in the
  notes of the Monado project (MIT). Only the notes were read; no code was copied.
