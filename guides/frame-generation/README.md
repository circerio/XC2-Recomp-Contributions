# Frame generation and Reflex in a game you cannot change

**Status:** DLSS multi frame generation (2x to 6x), FSR frame generation (2x) and NVIDIA Reflex are in our main line, all
off by default. Sections 2 to 7 describe how they work there. What is not finished or not looked at comes first.

A frame generator shows extra pictures between the ones the game draws, made by the vendor's library from what it is
handed each frame. A statically recompiled game has no engine to ask, so the port finds those inputs in what the game
already draws. This guide says where ours come from, how the swapchain goes to a library and back, how the HUD is kept
apart, and how we know that frames really are generated. It is for other people porting a console game to PC by static
recompilation, on Vulkan. You need no private code and no game files. There is no code with this guide.

**Tested on one machine only:** Windows 11, an NVIDIA GPU.
**Not tested: AMD, Intel, Linux.** FSR frame generation, too, ran on NVIDIA only.

## What has not been verified

- **The generated pictures.** We have no record of anyone judging generated frames one by one, and their quality is not
  measured.
- **Trails on the HUD.** The HUD is kept out of the interpolation (section 3). We have no record of anyone being asked
  whether it trails.
- **Latency.** The time from input to screen is not measured. In an early build, at the game's own 30, the driver's
  latency report did not differ between Reflex off, on and boost. [measured] So Reflex is known from logs only.
- **Camera cuts.** We reset a library's history after a pause or a change of size, not at a cut. A generated frame there
  may mix two shots. [inferred]
- **Even spacing on the screen.** DLSS at 2x was mostly even on one day, uneven on the next with the same build, and even
  after a restart of the PC. [measured] Cause not known. Above 2x we have not measured it.
- **The notice when it cannot run.** Every failure in section 6 was forced or simulated. We have no record of anyone
  seeing the notice on the screen.
- **The title screen.** The game treats its title as an event; the scope now counts it as play, and an event started
  from the title still counts as a cutscene. Two headless runs of the change before it was merged checked this, one
  through the title and one into such an event; the merged code is the same. [measured] No person has looked at it since
  the change.
- **FSR frame generation in cutscenes.** A person saw a flicker there (section 5), and it has not been confirmed on
  the machine since. The cause is not known.
- **HDR.** It ran, and nobody has judged the picture: see the [HDR guide](../hdr).
- **Vulkan validation.** With the layer on, Streamline's threads and our Reflex markers use the swapchain at the same
  time, and two early runs stopped. Runs without the layer went on to the end. [measured] Cause not known.

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[measured]** logged or measured by a program
- **[read]** taken from a vendor's public guide or source
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Feed each frame at the game's own TAA resolve, which already reads its depth and motion vectors. Read the camera from
   the constants that frame was drawn with. Show a frame without all of them as drawn. [section 2]
2. Copy the picture after the last post-processing draw and before the first HUD draw, scaled and encoded exactly as the
   frame. [section 3]
3. Each library wants the swapchain for itself. Choose whose functions make it before you make it. [section 4]
4. Use one test, "fed or not", for menus, loading, the scope and the cap. A pause never remakes the swapchain. [section 5]
5. Load the libraries and create their queues at every start, so that a menu row acts while the game runs. [section 5]
6. Expect failure: go back to off, say why, and try again only when the player asks. [section 6]
7. Let the frame rate cap count the frames shown, and give Reflex a limit for those too. [section 7]
8. Prove it by counting three things: presents, frames fed, and frames the library says it showed. [section 8]

## 2. What the game has to give

| Input | Where ours comes from |
|---|---|
| Depth, motion vectors | What the game's TAA resolve reads, the vectors decoded to pixels from this frame's position to the one before |
| Jitter | Read from that frame's TAA draw, not computed from a frame number |
| Camera | The constants the frame's own geometry was drawn with. DLSS takes matrices, position, axes and planes; FSR takes position, axes, planes and the vertical angle |
| Reset | Ours: the frame before was not fed, or the size changed |
| Picture without the HUD | Section 3 |
| Whether a cutscene plays | The game's own answer, asked once a frame on its thread. Only our scope uses it (section 5) |

- **Find the camera by its shape.** In the frame's first draws into several colour targets with depth, look for a view
  matrix whose rotation rows have unit length, followed by a perspective projection. Take the jitter out of the
  projection's third column, unless it is larger than a pixel: that is a lens shift, not the TAA's.
- **Previous from current.** Build that matrix from the product of the rotations and the difference of the translations,
  so that large world positions never enter it.
- **Check it against the game.** The position we computed matched the game's own camera object in every frame we sampled,
  with small differences while it moved. The jitter we took out was the one the TAA draw used. [measured]
- **DLSS reads the depth at the present,** after the post-processing. We mark it valid only at the resolve, so the library
  copies it there. FSR's prepare step is recorded at the resolve and needs no copy.
- **Hold the game's dynamic resolution off** while frame generation is on. Our inputs are given at the target's full size.

A trap: with FSR on and no upscaler, the library had taken the swapchain and nothing was fed. Our port read the camera
only for upscalers and DLSS, and our tests missed it, because a diagnostic switch in them also turned the camera on.
[measured] Test with the player's settings and no diagnostics, and log every frame not fed with its reason.

## 3. The picture without the HUD

Both libraries find the HUD by comparing a picture without it with the finished frame. [read] In our game the last
post-processing draw and the HUD's draws go to the same target, so after that draw we end the render pass and copy the
target, scaled as the frame is on the swapchain. Under HDR, where a pass of ours draws the frame, the same pass draws the
copy. A frame that was fed but had no such draw gives the frame as drawn, so nothing is kept apart in it.

With DLSS, the copy was handed over in every generated frame of the steady stretches and always came from that draw.
Turning it off did not change the spacing on the screen. [measured] We give no HUD layer with its own alpha.

## 4. Whose swapchain it is

**DLSS, through Streamline with manual hooking:**

- Start Streamline before the Vulkan instance, on the same thread. With a background thread some starts hung: a capture
  program's Vulkan layer had faulted while Streamline was starting. [measured] We think the loader's lock stayed with a
  thread that had ended. [inferred] In as many starts on the instance's own thread, none hung. [measured]
- Add its extensions, features and queues to the device. Then use its functions for creating and destroying the
  swapchain, its images, acquire and present.
- Make the surface again through Streamline once it knows the device: DLSS finds the window from the surface creation it
  saw. [read] With a surface made through the loader, every swapchain failed. [measured]
- Enable `VK_KHR_present_id` with `VK_NV_low_latency2`. The validation layer reported it missing. [measured]

**FSR:**

- It needs four different queues: the game's and three more. [read] If the graphics family has too few, we take the rest
  from other families; if there are still too few, frame generation is off and the game runs. On our GPU one family had
  all four, so the other paths are untested.
- Make a plain swapchain and hand it over. The library destroys it, returns its own, and gives you replacement functions.
  If the hand-over fails, your swapchain may be gone too, so make another.
- Ask for the image count first, the images after. Leave `oldSwapchain` empty when you remake it: the handle is not a
  Vulkan one. [read]
- Give it back in this order: generation off, its presents finished, the generation context, the swapchain context, the
  swapchain, then your own functions.
- It cannot take exclusive fullscreen: ours goes borderless while FSR runs, and takes exclusive back after. [measured]
- A change of HDR gives the swapchain back and hands over a new one: see the [HDR guide](../hdr).

**The two together.** With Reflex loaded, FSR's swapchain could not be made. [measured] Streamline's Reflex adds a
structure to the create info, and AMD's library refuses structures it does not know. [read] So we keep both sets of
swapchain functions, Streamline's and the driver's, and choose before making one. While FSR owns the swapchain, Reflex
does nothing, and its row says so.

Tell your present-mode rules which kind will present *before* you make the swapchain. Ours learned it from the first
generated frame, and the swapchain was made twice at a switch. [measured]

## 5. Pausing, the scope, and switching live

**One test.** A frame is fed only if its inputs are there, it is inside the scope, and the cap leaves at least two frames
(section 7). A frame not fed is shown as drawn: DLSS is set off with its memory kept, FSR is configured off. The next fed
frame resets the history. A full-screen menu draws no scene, so frame generation pauses there by itself. [measured]

**The scope** is read every frame, so a change counts at once: the whole game, only cutscenes, or everything except
cutscenes. With "only cutscenes", DLSS at 2x generated while an event played and paused when it ended. [measured] If the
game cannot say, the limited scopes generate everywhere.

The third value came from a report of flicker. Our port keeps cutscenes at the game's own 30, and AMD advises a base of 60
or more for the version that runs on our GPU. [read] With FSR at 2x, a person saw a character's eyebrows flicker in
cutscenes, not with frame generation off, and not with "everything except cutscenes". [seen] The cause is not known.

**Switching live.** We load every library that is there at every start, DLSS at off, and create FSR's queues whether it is
on or not. That costs start-up time and memory: measure it in your game. DLSS on and off, 2x to 6x, the scope and Reflex
follow the menu at the next presents. FSR on or off remakes the swapchain once, at the next present. FSR on and off, DLSS
to FSR and back, under HDR and in exclusive fullscreen, all switched without a failure in the log. [measured] For DLSS
and FSR that was the hand-over only: the signed DLSS library generated nothing there (section 8).

A pause keeps DLSS's memory. Turned off in the menu, it is kept while the display memory has room and freed a few presents
later if not. With a small memory budget simulated it was freed, and turning it on again cost one long frame. [measured]

**What else must be on or off.** DLSS frame generation needs Reflex [read], so the menu steps past Reflex off. Frame
generation of one vendor with super resolution of the other is not offered: the menu steps past it, and the runtime
refuses it at start. [measured] The [settings guide](../settings) covers such rows.

## 6. When it cannot run

Our first version failed silently: a failed call that turned DLSS on still counted as on, and a failed FSR left its row
reading "on" and was not retried until the next start. Now:

- These count as failure: less free display memory than the library's own estimate, asked before it allocates; the call
  that turns DLSS on failing; DLSS on for 120 presents in front with none generated; a present that runs out of memory;
  FSR's queues, swapchain or generation context failing.
- The setting goes back to off, in the file too, and the game goes on presenting. The row's caption in the settings menu
  says so, and whether it was the display memory, until the player turns a kind on again. Nothing pops up in play.
- The next time the player asks, it is tried again.

Eight runs, each with one failure forced or simulated, showed this: back to off, the file written, presents going on, and
another try where the run asked again. [measured]

## 7. The frame cap, VRR and Reflex

- **The cap counts the frames shown.** Our first rule counted the drawn ones, and a person found that a cap did nothing
  with 2x. [seen] Now the shown cap is the lower of the player's cap and the VRR ceiling. The frames used are the ones
  asked for, but no more than keep the game at its slowest allowed step, 30 in ours; fewer than two pauses it. The game
  draws the shown cap divided by the frames used. At a cap of 120, 6x is used as 4x; at 30 it pauses. The counts followed
  this at each cap we ran. [measured] The [VRR guide](../vrr) has the rules around it.
- **A 60 Hz screen.** With the VRR rules on, its ceiling is under 60, so even 2x pauses. Only unit tests cover that.
- **Reflex's frame limit counts shown frames too.** The guides we read did not say. We passed the game's own period, and
  at 6x the game slowed to a fraction of its speed. The period divided by the frames shown, but no shorter than the VRR
  ceiling's period, fixed it. [measured]
- **Present mode.** DLSS frame generation has no vsync on Vulkan. [read] Our rules use IMMEDIATE with it, and the
  player's choice with FSR. They follow the kind, not whether it generates at that moment. Our port loads the DLSS
  library at every start when its files are there and the GPU can run it, whatever the menu says, and the kind is set
  as soon as it has started. So with the VRR rules in use, IMMEDIATE and Reflex's limit apply from then on, even while
  frame generation is off. In a play with it switched off early, every swapchain made later was IMMEDIATE, and the
  limit stayed Reflex's. [measured]
- **Two pacers must not fight.** While the library generates, our own flip schedule steps aside. In a test build its log
  showed no held flip in those stretches. [measured]
- **Reflex markers.** Six for each frame, from the game's thread and the one that submits and presents. Steady stretches
  had 300 of each for 300 presents. [measured] Our sleep is at the end of the game's present call. At the game's 30 the
  driver's report showed the simulation taking a whole frame. [measured] We take that to be the game's own wait, which
  comes after our sleep. [inferred]

## 8. How we checked it

**By counting, over every 300 presents of the game.** For DLSS, Streamline's count of frames presented. For FSR, the
library's own present count, next to the frames we fed. With steady settings, DLSS at 2x, 3x, 4x, 5x and 6x showed exactly
that many times the presents, and FSR twice as many, or one fewer, with every frame fed. The builds merged into our main
line gave the same counts. [measured]

**By PresentMon.** Under a cap, shown divided by drawn was the multiplier at each step. Without one, near the screen's
rate, it counted fewer shown than presented. [measured] Cause not known.

**By pixels.** With frame generation off and HDR on, every pixel of 30 settled frames, in headless runs, was as before
FSR's HDR part of this work. [measured]

**By a person.** One person, on the machine above:

| Asked | Answer | Not asked |
|---|---|---|
| DLSS 2x in one cutscene: how it feels, and any black frames | It ran smoothly, no black frames | Single frames, HUD, latency |
| 2x to 6x switched live, with a frame counter | The counter rose at every step | The picture |
| The cap with frame generation on | Right | |
| FSR switched on and off in play | Right. The log shows two frames for each fed one, outside menus | The picture |
| FSR in cutscenes, without and with the third scope value | Without it, a flicker. With it, none, because frame generation then stops in cutscenes (section 5) | Other places |
| The three scope values, in play and in cutscenes | Right, except that the title screen should not count as a cutscene | The picture |

DLSS frame generation together with VRR is in the [VRR guide](../vrr).

**Traps in testing:**

- The signed DLSS library generates nothing while another window is in front. [measured] Log which window is. Our
  automated DLSS counts come from a development build of the library, from the public SDK, that can be told to run
  anyway, and are not the player's.
- Frames of multi frame generation are sent together and spaced by the display hardware. An ordinary PresentMon then shows
  pairs; the build that knows DLSS 4 adds the delay. [measured] Do not judge pacing by the average.
- "Its swapchain took over" proves nothing. Count the frames fed (section 2). Counts say that frames were shown, not that
  they look right.

## 9. What this guide does not cover

AMD's machine-learning frame generation, Intel and Linux; FSR in exclusive fullscreen or above 2x; a HUD layer with alpha
and a reset at camera cuts, which we have not done.

## Sources

There is no code in this guide.

- NVIDIA's Streamline SDK (its DLSS frame generation, manual hooking, Reflex and PCL guides) and AMD's FidelityFX SDK 2.4
  (its frame interpolation guides and public source). "[read]" refers to these.
- Our FSR frame generation follows the design of [AksharDP's xc2-recomp](https://github.com/AksharDP/xc2-recomp) (MIT):
  the queues, the order of taking over and giving back, and a wrapper for the library's function lookup, which does not
  check for missing functions. No code was copied. [Monado](https://github.com/vinitaison/Monado)'s notes also say to ask
  for the image count first, and to cut the picture before the HUD.
- Related guides here: [VRR](../vrr), [HDR](../hdr), [settings](../settings), [TAA jitter](../taa-jitter).
