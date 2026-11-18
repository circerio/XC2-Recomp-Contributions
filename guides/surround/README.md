# Surround output and positioning

**Status:** an interim approach. We will replace it with one faithful to the game's Nintendo Switch 2 Edition. Sections
2 to 5 describe what we did and checked in our own port. The output layouts and the positioning are part of our main
line; the back pair for 7.1 (section 4) is written and unit-tested, but not yet merged.

Our game sets up its sound output for six speakers, but puts every sound on the front left and right. This guide
describes how we output 5.1 and 7.1 when Windows has those speakers, and how we placed the game's positioned sounds
around the listener, using the direction the game already computes for each of them. It is meant for other people
porting a game the same way. You do not need any private code or game data to follow it: the reference code in
[`src/`](src) and its tests build on their own.

**Tested on one machine only:** Windows 11, headphones through a virtual surround renderer (Dolby Atmos for
Headphones), with Windows reporting 7.1 to the game. **Not tested: 5.1 or 7.1 speakers, Linux.** Nothing here claims to work there.

## What has not been verified, and known limits

- **Real 5.1 and 7.1 speakers.** We have none. Every listening check was on headphones.
- **Front and back on headphones.** On headphones, how well front and back can be told apart depends on the virtual
  surround that renders the speakers for them, not on this guide. In our blind listening test it was not reliable, with
  the positioning on or off (section 5).
- **Straight behind in our main line's 7.1.** In 7.1 our main line still places the back half of the circle on the side
  speakers, as in 5.1, so a sound straight behind comes from the two sides. The fix that puts it on the back pair
  (section 4) is not yet merged.
- **Changing the speakers in Windows while the game runs.** The port keeps the speakers it found at start.
- **Linux.**

Each result below says how we know it:

- **[seen]** a person looked at the game
- **[heard]** a person listened to the game
- **[measured]** logged or measured by a program
- **[read]** taken from a specification, a vendor document or the code
- **[inferred]** our reasoning, not proven

Angles below are in degrees around the listener: 0 straight ahead, positive to the right.

## 1. The short version

1. Measure what the game actually sends to each channel before adding outputs. Ours sends sound to the front left and
   right only. [section 2]
2. Let the number of channels follow Windows, and let the player choose stereo, 5.1 or 7.1. [section 3]
3. Order the channels by the device's channel mask, including both of Windows' 5.1 masks. [section 3]
4. Leave the stereo path as it was. [section 3]
5. Place only the sounds the game positions, by the direction the game computes for them, with equal power between the
   two neighbouring speakers at their standard angles. [section 4]
6. Leave unpositioned sounds and the LFE alone. Keep the positioning off by default, and when it is off, pass the game's
   own levels through bit for bit. [section 4]
7. Check each channel with test tones, and the stereo path frame by frame in the same run. [section 5]

## 2. What the game sends

We measured each of the game's six outputs in the title screen, a town, a battle and the cutscene after it. Only the
front left and right carried sound. The centre, the LFE and the surround pair were exactly zero throughout. [measured]
Reading the game's sound code, we found why. It pans every sound between left and right only, and it has no surround
mode that the port leaves switched off. [read]

For each sound it positions in the world, the game computes the sound's direction from the listener, front and back
included, and uses only its left-right part. [read]

So with a stereo output nothing of the game's is lost. A 5.1 or 7.1 output on its own changes nothing you can hear: the
surround speakers stay silent. [inferred]

## 3. The output: as many channels as Windows has

The reference code is [`src/surround_pan.hpp`](src/surround_pan.hpp).

- **Follow Windows.** By default the port asks the audio device which speakers it has (its channel mask). It outputs
  7.1 if the device has all of 7.1, else 5.1, else stereo (`choose_mask`). The player can choose stereo, 5.1 or 7.1
  instead. A choice the device lacks falls back to stereo, and the menu says why. The choice can be changed while the
  game runs. [read]
- **Order the channels by the mask.** In a multichannel stream the channels come in the order of the mask's speaker
  bits, lowest first. [read] Windows has two 5.1 masks with one surround pair each: one names the pair "back", the other
  "side". [read] We put the game's surround pair on whichever pair the mask has. In 7.1 the back pair comes before the
  side pair in the stream, although the sides stand further forward. The game's surround pair goes to the sides, and
  the back pair is left for the positioning (`channel_of`, `route`). [read]

  | Output | Mask | Channels in order |
  |---|---|---|
  | Stereo | 0x3 | FL FR |
  | 5.1, "back" | 0x3F | FL FR FC LFE BL BR |
  | 5.1, "side" | 0x60F | FL FR FC LFE SL SR |
  | 7.1 | 0x63F | FL FR FC LFE BL BR SL SR |

- **Connect each channel yourself.** We connect each of our channels to the device's speaker with the same bit
  ourselves. XAudio2 does not document its default connection for this case. [read]
- **The stereo path stays as it was.** With a stereo output the port mixes exactly as it did before it had surround:
  the game's centre and surround pair folded into the fronts, and the LFE dropped. All of these are silent anyway.
  [read] [measured, section 5]

## 4. Placing the game's positioned sounds

- **Which sounds.** Only mono sounds that the game positions in the world. Music, menu and system sounds, sounds the
  game plays without a position (dialogue among them), stereo and multichannel sounds, and the game's reverb and delay
  all keep the game's own levels. Dialogue that the game positions in the world is placed like any other positioned
  sound. Nothing is ever placed on the centre or the LFE. [read]
- **The direction is the game's.** The left-right part comes from the game's own pan. The front-back part comes from the
  same direction, which the game computes but does not use. The angle is the arctangent of the two (`azimuth`). [read]
- **The level is the game's.** We spread the total power of the game's own two gains over the speakers
  (`amplitude_of`), so placing a sound never makes it louder or quieter overall. Straight ahead, each front speaker keeps
  the game's own level. [inferred]
- **Equal power between neighbours, at standard angles.** The speakers stand on a circle. A sound between two
  neighbours gets the cosine and the sine of how far along it is, times 90 degrees. On a speaker's own angle, it comes
  from that speaker alone (`spread`). [read]
  - **5.1:** the fronts at ±30 and the surround pair at ±110. ITU-R BS.775 places the surround pair anywhere from 100
    to 120 degrees, and ITU-R BS.2051 gives 110 as their angle in its 5.1 layout. We use 110 for both of Windows' 5.1
    masks: "back" in a mask's name is not where the speakers stand. [read]
  - **7.1:** the fronts at ±30, the sides at ±90 and the backs at ±135, the nominal angles of ITU-R BS.2051's 7.1
    layout. A sound straight behind is split equally between the back pair. [read]
  - **In our main line, 7.1 uses the 5.1 circle** on the front and side speakers, and the back pair stays silent. In a
    7.1 layout the side speakers stand at about 90 degrees, beside the listener, so a sound placed straight behind
    comes from beside. The fix that uses the back pair is written and unit-tested, and is not yet merged. Our game's mix has no back pair, so the fix adds one;
    where it cannot, it falls back to the 5.1 circle (`ring_for`). [read]
- **Off by default, and off means off.** The positioning has its own switch, off by default. Choosing the output
  automatically never turns it on. With it off, or with a stereo output, the game's own levels pass through untouched,
  bit for bit (`place`). If your mixer keeps a sound's levels until they are set again, set the surround ones to zero
  when the positioning goes off, so that nothing is left behind. [read]

## 5. How we checked it

- **Each channel in its place.** A test mode played a tone on one channel at a time, each speaker at its own pitch. In
  recordings of the stereo, 5.1 and 7.1 output, every tone was on its own channel only, the others exactly silent, in
  the order the log gave. [measured]
- **Stereo unchanged.** In the same run, the port also computed the stereo mix with the code it had before surround, and
  compared every frame. Not one frame differed: neither while we switched between outputs, nor with the positioning
  switched on at a stereo output. [measured] Comparing two runs would not prove this. When the game's sounds start
  depends on thread timing, so two runs of one build are not the same bit for bit. [inferred]
- **On and off.** With a 5.1 output in a town, the surround channels were exactly silent before the positioning was
  switched on. They carried sound while it was on, and were exactly silent again after it was switched off. In a battle
  with the positioning on, they carried sound too. [measured]
- **The rules as unit tests.** The checks are: the total power, left-right symmetry, a sound on a speaker's angle coming
  from that speaker alone, off passing the game's levels through bit for bit, unplaced sounds untouched, the LFE never
  written, and the channel order of each mask, with the side and back pairs not swapped. All of them passed with the
  back pair fix. Our main line's own tests cover the total power, the main directions and a direction that is not one,
  and pass. [measured] The tests here check the same rules on the reference code.
- **Listening, on headphones.** In a blind test, noise bursts came from eight directions in random order, once with the
  positioning on and once with it off. The answers were opened only afterwards. Left and right were never mixed up, on
  or off. Front and back were not reliable either way. A sound straight behind, split between the two side speakers,
  was not heard behind in either of its two trials. [heard] In play, with the positioning on, the sound felt more
  around the listener and moved smoothly as the camera turned. [heard]
- **Then by the rules, not by ear.** After that test we judge the placement by its rules: the speakers at their standard
  angles, and equal power between neighbours, both checked by the unit tests. The back pair for 7.1 comes from the
  standard placement, not from listening.

## 6. What this guide does not cover

- **The Switch 2 Edition's own surround.** This guide describes an interim approach; see the top.
- **The centre speaker.** Nothing is placed on it.
- **Height speakers and spatial audio objects.**
- **Linux.** Not tested.

## 7. Build and run the tests

Any C++17 compiler works; there are no dependencies.

```sh
cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
```

or by hand:

```sh
g++ -std=c++17 -Isrc tests/test_surround_pan.cpp -o test_surround_pan && ./test_surround_pan
```

## Sources

The code here was written for this guide and is under the MIT licence of this repository. It contains no third-party
code. The speaker angles follow these documents; the channel order and masks follow Microsoft's.

- ITU-R, Recommendation BS.775-4 (12/2022), *Multichannel stereophonic sound system with and without accompanying
  picture*, "recommends" 2: the front pair and the side/rear loudspeakers:
  [R-REC-BS.775](https://www.itu.int/rec/R-REC-BS.775-4-202212-I/en)
- ITU-R, Recommendation BS.2051-3 (05/2022), *Advanced sound system for programme production*, Table 1 (the nominal
  angles), Table 4 (Sound System B, 0+5+0) and Table 11 (Sound System I, 0+7+0):
  [R-REC-BS.2051](https://www.itu.int/rec/R-REC-BS.2051-3-202205-I/en)
- Microsoft, `WAVEFORMATEXTENSIBLE` (the channel order):
  [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible)
- Microsoft, `KSAUDIO_CHANNEL_CONFIG` (the two 5.1 speaker configurations, with back or side speakers, and 7.1):
  [learn.microsoft.com](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ksmedia/ns-ksmedia-ksaudio_channel_config)
- Microsoft, Channel mask (the 7.1 mask 0x63F and its channel order):
  [learn.microsoft.com](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/channel-mask)
- Microsoft, `IXAudio2Voice::SetOutputMatrix` (the output matrix):
  [learn.microsoft.com](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-setoutputmatrix)
