# Variable refresh rate (G-SYNC / FreeSync) in a native Vulkan PC port

**Status:** sections 2 to 6 describe things we have seen work or measured on the machine below.
[Section 5](#5-the-refresh-meter-jumps-at-a-locked-frame-rate) was waiting on verification in the first version. It now has measured
results, and its fix is on by default in our main line.

This describes what we did and what we saw while adding variable refresh rate (VRR) to a native Windows port that presents
through Vulkan. It is meant for other people porting a game the same way. You do not need any private code or game data to
follow it: the reference code in [`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, an NVIDIA GPU, one 240 Hz G-SYNC Compatible OLED monitor with a range of
48 to 240 Hz, NVIDIA Control Panel set to "Enable G-SYNC for windowed and full screen mode".
**Not tested at all: AMD, Intel, Linux, a monitor without VRR.** Nothing here claims to work there.

Each result below says how we know it:

- **[seen]** a person looked at the NVIDIA indicator or the monitor's own display
- **[measured]** logged by the program
- **[read]** taken from a specification or vendor document
- **[inferred]** our reasoning, not proven

## 1. The short version

1. Cover the monitor with a borderless window, or take exclusive fullscreen. Both engaged VRR for us; an ordinary
   decorated window did not. [seen]
2. Under the cap, use FIFO for every vsync-on setting. MAILBOX has nothing to gain there. [inferred, section 2]
3. Cap the frame rate a little under the refresh rate, unless the player chose something lower.
4. Do not gate the feature on a vendor query. Ours answered "success" with every bit 0 while VRR was running. [seen,
   measured]
5. Check that VRR is on with the driver's indicator and the monitor's own display, not with present timing. We read
   the timing and concluded VRR was off. It was on. [seen]
6. If a monitor's refresh meter jumps around at a locked frame rate, look at the intervals between the actual flips, not
   at the thread that paces the game. Holding each flip to a schedule evened them out for us. [measured, section 5] The
   meter then held at 60, but it still jumped now and then while the camera turned. [seen] With an empty pipeline cache, most of the
   slow frames there built a graphics pipeline for the first time; with the cache filled, few did. So if the meter still jumps once
   the flips are even, look at first-time pipeline builds before the pacing. [measured, section 5]
7. When you test FIFO on an unattended machine, keep the display on. With the display off, FIFO ran at one frame every
   250 ms and looked like a bug in the code. [measured]

## 2. The rules

The reference code is [`src/vrr_policy.hpp`](src/vrr_policy.hpp); [`tests/test_vrr_policy.cpp`](tests/test_vrr_policy.cpp)
checks every row. It has no Vulkan and no OS calls, so all of this can be unit tested.

The rules apply when all of these are true: the setting is on, the window is borderless fullscreen or exclusive
fullscreen, the refresh rate is known and at least 48 Hz, and no vendor query has said "no VRR". Otherwise everything
behaves as if the feature did not exist.

| VRR rules | Window | Vsync: low latency | Vsync: standard | Vsync: off | Frame rate cap |
|---|---|---|---|---|---|
| Apply | Borderless or exclusive | FIFO | FIFO | IMMEDIATE | Refresh rate − 3 (240 → 237, 144 → 141); a lower cap of the player's wins |
| Do not apply | Decorated window | MAILBOX | FIFO | IMMEDIATE | The player's |
| Setting off | Any | MAILBOX | FIFO | IMMEDIATE | The player's |

Why these choices:

- **FIFO under the cap.** With the frame rate held under the refresh rate, the display refreshes when a frame arrives,
  so a FIFO present has no queue to wait in and MAILBOX cannot lower the latency. [inferred] NVIDIA's Reflex
  documentation also lists vsync on with VRR on as a latency-reducing combination. [read]
- **The cap.** A frame faster than the refresh would wait for it under FIFO, or could tear under IMMEDIATE. We use 3 fps
  and did not compare other margins, so treat it as a starting point rather than a measured best value.
- **A decorated window keeps its old behaviour.** It is composited by the desktop, so VRR depends on the driver setting,
  and FIFO on a fixed refresh rate waits for it. When we tested it, the indicator did not appear in a decorated window even with
  "windowed and full screen" selected. [seen] We did not find out why, and for us it was not worth pursuing.

Present mode is fixed when a swapchain is created, so when a fact changes, recreate the swapchain before the next present.
Read the refresh rate of the monitor the window is on (`MonitorFromWindow`, `GetMonitorInfo`, `EnumDisplaySettings`) when the
window opens, after a fullscreen change, on `WM_DISPLAYCHANGE`, and every few seconds. In a test that switched between
exclusive, borderless and windowed four times, the largest gap between frames around a switch was about 93 ms. [measured]

We compared present modes at a 60 fps cap, one run each. In borderless, MAILBOX and FIFO came out the same. In a decorated window,
MAILBOX was steadier and reached the screen sooner, which is why a decorated window keeps it. [measured]

## 3. Which windows get VRR

| Mode | Indicator | How it was made |
|---|---|---|
| Exclusive fullscreen | Yes [seen] | `VK_EXT_full_screen_exclusive`, application controlled |
| Borderless window covering the monitor | Yes [seen] | A `WS_POPUP` window the size of the monitor |
| Decorated window | No [seen] | |

Notes on exclusive fullscreen:

- Enable `VK_KHR_get_surface_capabilities2` and `VK_EXT_full_screen_exclusive`, and ask the surface whether it supports
  exclusive mode. Treat "no" as borderless.
- Create the swapchain with application-controlled exclusive mode, then call `vkAcquireFullScreenExclusiveModeEXT`. If that
  does not succeed, retry about once a second.
- Handle `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT` like an out-of-date swapchain: recreate and acquire again.
- On losing focus, release exclusive mode and minimise. Acquire it again at the next present after returning. Alt+Tab and
  F11 behaved correctly in testing. [seen]
- We kept the desktop's display mode and scaled on the GPU, so changing modes does not make the monitor resynchronise.
- We saw no difference between exclusive and borderless in the numbers: both had the same interval distribution
  at the caps we ran. [measured]

## 4. How to tell whether VRR is on

Two things misled us, and either could cost you a day.

**A vendor query answered with zeros.** `NvAPI_Disp_GetVRRInfo` returned success with every field 0 (possible, enabled,
requested, indicator and "in VRR mode"), both in the game and when read from the desktop, while the indicator showed VRR running.
Our first version treated "possible = 0" as "this display has no VRR", so the rules never applied, and seven timing runs
silently measured the old path. Treat all zeros as "unknown". This has a cost: you cannot detect a monitor without VRR this
way, so on such a monitor at 60 Hz the cap becomes 57 and about 3 frames a second are repeated. Reading the range from the
monitor's EDID or using a vendor API such as AMD's ADLX might avoid that. We have not tried either.

**Present timing looked like a fixed refresh rate.** With the display on, the time from present to on-screen looked like it
included an extra wait of 0 to 4.17 ms, exactly what you would expect from waiting for the next 240 Hz refresh, so we concluded
VRR was probably not engaged. The indicator then showed that it was. Do not
infer VRR from present timing.

What does work:

1. In NVIDIA Control Panel, under "Set up G-SYNC", enable it for windowed and full screen mode, for the monitor in use.
2. Turn on the indicator: Display menu, "G-SYNC Compatible Indicator" (or "G-SYNC Indicator"). It appears over the game
   while VRR is active.
3. Use the monitor's own refresh display. Cap the game at 144 on a 240 Hz monitor: it should read about 144, not 240.

## 5. The refresh meter jumps at a locked frame rate

The symptom: at a 60 fps cap, the refresh meter of the monitor kept jumping between about 60 and 120, sometimes past 200. The
picture did not visibly stutter, only the number moved. At a 144 cap the meter was normal. [seen]

What we found, in three layers:

1. **The timing of the flips.** The flip happens when the render thread has finished its work, which the game thread's
   pacing does not control. Flip intervals followed the changes in render work (correlation 0.67 to 0.75). A flip that comes late
   is followed by an early one, so the intervals alternate long and short even though the average is exactly 60 fps.
   [measured] Improving the pacing of the game thread alone made no visible difference. [seen]
2. **The panel's lower limit.** This monitor's range starts at 48 Hz, or 20.83 ms. A longer gap forces the panel to rescan,
   and the meter then reads high. Some intervals did go over that limit. [measured] A 60 fps cap leaves 4.2 ms of margin to it and a
   144 cap leaves 13.9 ms, which may be why 144 looked normal. [inferred] We also saw the meter sit at 120 while 60 consecutive flips,
   measured with PresentMon, were evenly spaced, so not all of it comes from our flips. This looks like the driver's low framerate
   compensation, but we cannot see what the driver does and have not proven it. [inferred]
3. **Spikes in the work.** Turning the camera roughly doubled the render thread's work, and that is where the frames over the panel's
   limit gathered. Moving texture copies to a worker and sub-allocating images brought their number down a lot. [measured] (Both are
   separate pieces of work and are not part of this guide; both are now on by default in our build. Sub-allocating through VMA
   follows what xcx-recomp does.)

### Keeping a schedule for the flips

[`src/flip_pacer.hpp`](src/flip_pacer.hpp) holds each flip until it is due, on the thread that flips. A flip more than 2 ms late
starts the schedule again from itself, instead of shortening the next interval. The 2 ms allowance stops a timer's
lateness from lowering the frame rate.

It stays out of the way in two cases. With no frame rate cap there is no schedule to keep. And while a frame generation library
paces the presents itself, the schedule leaves the timing to it, so the two do not fight over it.

What someone watching the monitor's meter saw at a 60 fps cap, in short runs of about 30 seconds while we were working on this: [seen]

- **Pacing only the game thread:** it jumped past 100 even when standing still.
- **The flip schedule:** very steady when standing still, but it still jumped when moving and turning, and once stuck at 120.
- **The flip schedule and fewer work spikes:** steady while turning the camera, passing 100 about once in 10 seconds of turning.

**Read this for its direction only.** The runs were short, what the player was doing differs between them, and this is someone
watching a number, not a measurement.

[`tests/sim_flip_pacing.cpp`](tests/sim_flip_pacing.cpp) shows the mechanism on a synthetic trace. It proves nothing about a real
machine, but it shows the cost as well as the gain: holding flips adds latency, up to about the size of the largest spike.

```
synthetic trace, 3600 frames, period 16.67 ms, intervals in ms
                           min      p1     p50     p99     max     <10 ms  >20.83 ms   lat avg   lat max
flip when rendered        5.00    7.32   16.67   26.42   28.93         72        111      5.77     17.51
through FlipPacer        15.66   16.67   16.67   16.67   21.67          0          1     16.29     17.51
```

The average latency in the last row is the model's worst case: nothing in it ever lets the schedule catch up again, so after the largest
spike every frame keeps that delay. In our short real tests the player noticed no change in feel, but we have not measured latency. Measure
it on your own game before adopting this.

### What we measured

We then compared the same build with the schedule on and with it turned off: a 60 fps cap, borderless, frame generation off, one run
of each lasting several minutes. PresentMon's display times give the interval between frames actually reaching the screen. [measured]

- **Off:** 60 frames a second still reached the screen, but only 76.1% of the intervals were within half a millisecond of 16.67 ms. Most
  of the rest landed one refresh of the 240 Hz panel early or late, at 12.5 or 20.8 ms, all through the run and not only when the camera
  turned. The presents themselves came a little early or late, and their frames reached the screen one refresh sooner or later.
- **On:** 99.6% of the intervals were within half a millisecond of 16.67 ms. Nearly all of the rest fell in the short stretch where a new
  area loaded and the camera turned.
- **The longest gaps were about the same either way**, and the longest of all came while loading a new area. The schedule evens out
  the ordinary frames; it does not remove spikes in the work.

In exclusive fullscreen at a 60 fps cap, the schedule held as well: the few frames that missed it were ones the game itself submitted
late, and the schedule started again from them as designed. With no cap it stayed out of the way, and it stepped aside while frame
generation was producing frames. [measured] That last check was made in a test build, since frame generation is not in our main line
yet.

These are intervals on the screen, not the meter, and nobody watched the meter during these runs. We once saw it sit at 120 while the
flips were evenly spaced (layer 2 above), so even intervals alone may not keep it still on every monitor. [inferred]

### What the meter showed afterwards

Once the schedule was on by default in our main line, someone watched the monitor's own refresh display at a 60 fps cap, in
borderless, on the monitor above. [seen]

- Standing still and moving normally, it held at 60.
- It still jumped upward now and then while moving and turning the camera. This was not often, and the person watching did not find it
  distracting.

To see what those moments were, we ran the same walk twice at a 60 fps cap in borderless, logging every frame next to PresentMon's
display times: first with an empty pipeline cache, then again with the cache the first run had filled. [measured]

- **With an empty cache, most single frames that went past the panel's lower limit had a graphics pipeline built for the first time on
  that very frame.** [measured]
- **With the cache filled, few did.** There were clearly fewer long intervals while moving. Most of those left came from texture uploads
  while moving and from the game's own frames arriving late. [measured]
- The schedule did not send frames early: intervals well short of 16.67 ms were very rare in both runs. [measured]
- Later, in a test build, converting new textures out of the console GPU's tiled layout on a background thread instead of on
  the render thread clearly cut the slow frames from texture uploads while walking, at a small cost on frames without new
  textures. It is now on by default in our main line. [measured]

The long intervals clustered at 20.83 ms plus whole refreshes of the 240 Hz panel (4.17 ms). That fits the panel showing the old frame
again at its 48 Hz limit, and the meter reading high when it does. But PresentMon sees the flips, not the panel, and nobody watched the
meter during these two runs. [inferred] A schedule can delay a flip but not bring a late one forward, so it cannot remove these frames.

We have not watched the meter in exclusive fullscreen yet. Of the measurements in this section, only the exclusive fullscreen run
under "What we measured" was made there, and it shows only that the schedule held. The two runs with the pipeline cache were in
borderless, and we have not checked exclusive fullscreen this way.

## 6. A testing trap: FIFO with the display off

Unattended timing runs under FIFO suddenly ran at one frame every 250 ms, about one second behind, while IMMEDIATE and MAILBOX were normal.
An older build that had been fine failed the same way, so it was not the code. Windows was turning the monitor off after 15 minutes idle,
and our visible test jobs started after 20. With the display kept on, FIFO was normal. [measured] We infer that without real
refreshes FIFO has nothing to wait for, though one earlier idle run was fine, so this is not fully explained.

Later tests on the same machine: turning the display off for 13 seconds mid-run gave one 404 ms gap on waking and about 5 seconds of dropped frames
before returning to 60; a job that wakes the display at its start ran normally throughout. [measured]

If you test without a person at the machine, wake the display, keep it on, and record that it was on. MAILBOX and IMMEDIATE do not depend on it, so they will not warn you.

## 7. What this guide does not cover

- **AMD, Intel, Linux.** Not tested. The rules treat an unknown display as VRR-capable, so a monitor without VRR is capped below its refresh rate.
- **Frame generation and Reflex together with VRR.** We have not verified these, so we make no recommendation.
- **Why a decorated window did not engage VRR.** Not investigated.
- **HDR with exclusive fullscreen.** Runs without errors, but nobody has checked the picture.
- **Flicker.** OLED panels can show brightness changes when VRR's rate swings widely. We did not study this.

## 8. Build and run the tests

Any C++17 compiler works; there are no dependencies.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_vrr_policy.cpp -o test_vrr_policy && ./test_vrr_policy
g++ -std=c++17 -Isrc tests/test_flip_pacer.cpp -o test_flip_pacer && ./test_flip_pacer
g++ -std=c++17 -Isrc tests/sim_flip_pacing.cpp -o sim_flip_pacing && ./sim_flip_pacing
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party code. Referenced documents:
the Vulkan specification (`VK_EXT_full_screen_exclusive`, `VK_KHR_present_wait`), NVIDIA's Reflex programming guide in the
Streamline SDK, and NVIDIA's documentation for its G-SYNC indicator.
