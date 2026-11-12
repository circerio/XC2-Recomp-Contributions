# Internal and output resolution

**Status:** sections 2 to 8 describe what we did and measured in our own port. All of it is in our main line.

A game made for one fixed size draws everything at that size: the 3D scene, the effects and the interface. In a PC port
you want the scene at a resolution of its own (internal), and the interface and text sharp at the screen's resolution
(output), without changing the game. This guide describes how we did that for a game that renders at 1280x720: which
images get which size, what has to follow, where the scene and the interface part ways, and why distance-field text needs
one more step. It is meant for other people porting a game the same way. You do not need any private code or game data to
follow it: the reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, output up to 3840x2160.
**Not tested: AMD, Intel, Linux.** Nothing here claims to work there.

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or measured by a program
- **[read]** taken from a specification or from another project's source
- **[inferred]** our reasoning, not proven

A scale below is relative to the game's own pixels: the game's 1280x720 shown at 3840x2160 is a scale of 3.

## 1. The short version

1. Give each image a scale when it is made: the presented textures the output scale, the scene's screen-sized render
   targets the internal scale, everything else 1. [section 2]
2. Scale everything the game gives in pixels: viewports, scissors, copies, the window crop. [section 3]
3. Let the translated shaders keep working in the game's pixels. A translator built for resolution scaling, such as
   Ryujinx's, already emits the code; feed it the right scale per draw. [section 4]
4. Split the frame after the anti-aliasing resolve: from there on, draw into output-size copies of the scene images, so
   tone mapping and the interface run at the output size. [section 5]
5. When one pass binds attachments at two scales, make the smaller one larger, for good if the scene never uses it.
   [section 6]
6. If the game lowers its own resolution, accept its smaller targets, and leave that off by default once the picture is
   not at the game's own size. [section 7]
7. Distance-field text gets blurrier with the output scale. Narrow its edge by the scale, up to 2. [section 8]

## 2. Which images get which scale

The reference code is [`src/render_scale.hpp`](src/render_scale.hpp) (`classify`).

| Class | Which images | Scale |
|---|---|---|
| Window | The textures the game presents | Output |
| Scene | 2D, single-sample render targets the size of the game's frame | Internal |
| Other | Everything else: half-size effects, shadow maps, lookup tables, interface art, textures the CPU uploads | 1 |

- **Only the full-size scene targets.** Effects the game draws at half size or smaller stay at their size; the shaders
  reading full-size images still find the right texels through section 4. [measured]
- **Decide when the image is made**, from its size and its first use. A screen-sized image that is first sampled and only
  later drawn into, such as an anti-aliasing history, becomes a scene image the first time it is a target.
- **Images a shader writes as storage, or the CPU uploads, go back to the game's size.** Their writes are in the game's
  pixels, and the translator rescales reads only. [inferred]

## 3. Commands given in the game's pixels

| Command | What to do |
|---|---|
| Viewport | Multiply by the target's scale |
| Scissor | Multiply, then take the smallest whole-pixel rectangle that covers it, inside the pass (`scale_scissor`) |
| Clear | The whole image, or a scaled rectangle |
| Copy between images | A plain copy at the same scale; a scaled blit otherwise, linear for colour and nearest for anything else (`copy_kind`) |
| Window crop at present | Multiply by the window texture's scale |

Debug captures that keep a ring of frames are worth saving at the game's size, or the ring grows with the square of the
scale.

## 4. Shaders: work in the game's pixels

Ryujinx's shader translator can already emit resolution scaling code: it divides the fragment position by the target's
scale, multiplies texel-fetch coordinates by the texture's scale, and divides size queries by it. A runtime that never
fills those values gets the game's behaviour. [read] Ours had been passing 1 for everything; giving the real scales per
draw was enough for the game's shaders to keep working in the game's pixels at the scales we tried. [measured]

What each draw passes for a bound texture (`texture_scale_value`), following Ryujinx's rule: [read]

- **Textures at scale 1:** nothing; the default of 1 holds.
- **A fragment stage reading a texture at its target's scale and proportions:** a negative scale, which tells the shader to
  fetch by its own pixel position. Each pixel then reads its own texel instead of the one at the game's coarser position.
- **Any other stage or texture:** the texture's scale.
- **A geometry stage:** nothing can be passed. Ryujinx emits the rescaling code for vertex, fragment and compute stages
  only. We inspected our game's geometry shaders: each only samples with normalised coordinates, so the scale does not
  affect them. The picture showed no misplaced effect. [measured]

## 5. The scene and the interface apart

To keep the scene at the internal resolution and still draw the interface sharp, the frame is split after the draw that
resolves the game's temporal anti-aliasing, the last draw that needs the scene's own pixels.

- From that draw to the present, any draw into a scene image goes to that image's **twin**, an output-size copy. The first
  time, the twin is seeded with what the scene left in the image, scaled up. Reads during the frame go to the twin too.
  (`Twins` in the reference code.)
- So bloom compositing, tone mapping, sharpening and the interface all run at the output size, while the scene itself
  stayed at the internal size. [measured]
- **The seed is where an upscaler goes.** A temporal upscaler writes its output into the twin instead of the seed.
- At the present the twins retire. Their content is scaled back into the scene images before the next frame's first draw,
  so a later frame that reads one sees what it would have seen. [measured]
- **Packed values are not interpolated.** If a pass after the split reads a value the game packed into several channels
  (in our game, a depth split into whole and fractional parts), bilinear sampling across scales mixes the parts into
  nonsense. We sample that one binding with nearest filtering. [inferred]

With output at 3840x2160 and the scene at 1280x720, the interface's text and icons came out clearly sharper than a plain
upscale of the whole frame, and the scene the same as a bilinear upscale, as intended. [measured] With the scene at
2560x1440 or 3840x2160, edges in the scene gained real detail, not sharpening: scaled back down, the picture stayed close to
the game's own. [measured]

## 6. One pass, two scales

Sometimes one pass binds attachments that ended up at different scales. In our game the interface pass binds the presented
texture (output scale) together with a 1280x720 depth buffer that only the interface and the sharpening use. [measured]
(`fix_pass`.)

- **An image the scene never draws into:** make it again at the larger scale, for good, keeping its content scaled.
- **A scene image:** give it a twin for the rest of the frame (section 5).
- **Anything else:** log it rather than guess.

## 7. The game's own dynamic resolution

Our game lowers its resolution by making smaller targets, in steps of the same aspect ratio, not by drawing into a smaller
part of the same ones. [measured] So the scene class takes any screen-sized target of the same aspect within 2%, more than
half the game's height and at most all of it (`screen_sized`).

The game decides when to step down from its own GPU timer queries. What those measure in a port is not what they measured
on the console: in ours, a busy machine made the game step down even at a high internal resolution, and every step's
targets were made at that resolution. [measured] So our main line leaves the game's own stepping off by default whenever
the picture is not at the game's own size:

- **The player chose an internal resolution.** They asked for that size.
- **The output is larger than the game's 1280x720.** A 1280x720 scene shown at 3840x2160 is already stretched three
  times, and the game's stepping took it lower still. [measured]
- At the game's own 1280x720 output, with no internal resolution chosen, nothing changes: the game steps as it always
  did.

A player who wants the stepping anyway can turn it on in the menu, and that choice wins over the default, except while
an upscaler or frame generation is on, which keeps the game's stepping off either way. The menu row says that it is off
by default above 720p.

## 8. Distance-field text

Our game's text comes from distance-field atlases: each glyph's outline is stored as a distance, and the text shader draws
the edge between two fixed values of it. Because that edge is fixed in the field's units, it grows with the drawn size. At
3840x2160 the edges of the game's text measured clearly wider than at 1280x720, while our own icons, drawn from larger
art, did not. [measured] The atlases themselves were fine enough; upscaling them would not change the edge. [inferred]

`narrow_edge` keeps the centre of the edge, so the stroke weight stays, and divides its half width by the output scale,
up to 2. We give the text shaders that changed value in place of the game's.

- Without the cap, very thin strokes, whose field barely reaches the centre, start to break up. We chose 2 from a simulation
  outside the game that drew its atlases with its shader's formula. [inferred]
- In a test build at 3840x2160 the edges of the game's text came out clearly narrower, with the thin strokes intact; at
  1280x720 the picture did not change. [measured] It is on by default in our main line.
- A shader that measured the edge from screen-space derivatives would avoid this altogether, but that means changing the
  game's shaders. Changing one value was enough. [inferred]

## 9. What this guide does not cover

- **Constants in the game's uniforms that hold a texel size.** Some effects step by one game pixel through a constant the
  game computed. Those keep stepping by one game pixel, slightly softer at a higher internal scale. We have not converted
  them.
- **Effects at half size or smaller.** They stay at their size.
- **Interface art.** Bitmaps made for 1280x720 are still scaled up; only text is fixed here.
- **Screen-door transparency.** Patterns based on the pixel position grow with the scale, the same as in Ryujinx.
- **AMD, Intel, Linux.** Not tested.

## 10. Build and run the tests

Any C++17 compiler works; there are no dependencies.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_render_scale.cpp -o test_render_scale && ./test_render_scale
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code.
The shader side follows Ryujinx's resolution scaling: the render scale its shader translator emits code for, and the rule
in its `TextureBindingsManager.UpdateScale` for which textures a fragment stage fetches by its own pixel position
(Ryujinx, MIT, Ryujinx Team and Contributors). No code was copied.
