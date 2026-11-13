# XC2-Recomp Contributions

Technical notes, small tools and patches from [XC2-Recomp](https://github.com/circerio/XC2-Recomp), research into making
Xenoblade Chronicles 2 as good as it can be on PC. XC2-Recomp does not aim to release a playable version and does not
explain how to play the game with it. Its techniques are published here as implementation guides, for anyone porting
Nintendo Switch games to PC by static recompilation; anyone who wants these features in a playable PC port is welcome to
merge them by following the guides. Everything here builds and reads on its own: you do not need XC2-Recomp's source,
which is not public, and you do not need any game files.

"Xenoblade Chronicles 2: REFORGED Edition" (an unofficial fan work, not affiliated with Nintendo or Monolith Soft) is the
final form this research aims for, and also our name for the version we play ourselves; it will not be released. A logo
for it will be made, and we hope future integrators will use it.

This work stands on what other people have published. In particular, **Ryujinx** (`Ryujinx.Graphics.Shader`),
[**SuperMarioWonderRecomp**](https://github.com/BluetufieBugado/SuperMarioWonderRecomp), [**AksharDP's xc2-recomp**](https://github.com/AksharDP/xc2-recomp),
[**jmars125's xcx-recomp**](https://github.com/jmars125/xcx-recomp) and **Monado** are the projects these notes most often refer to. Where something here builds on someone else's work, the page says so, and credit for those parts stays with them.

XC2-Recomp does not compete with any PC version of this game or any project working toward one: not with TheMonadoGuy and
Monado, which inspired this project, and not with those that came later, such as AksharDP's xc2-recomp. Our warm thanks to
TheMonadoGuy for that inspiration.

## Contents

| Topic | What it is | Status |
|---|---|---|
| [Variable refresh rate](guides/vrr) | Guide with reference code: present modes, the frame rate cap, verifying VRR, and a refresh meter that jumps at a locked frame rate | Tested on one NVIDIA machine; the guide lists what has not been verified |
| [HDR output](guides/hdr) | Guide with reference code: HDR10 and scRGB from a game made for SDR, peak brightness and paper white, keeping highlights and their hue | Tested on one NVIDIA machine; the guide lists what has not been verified |
| [Auto exposure at any frame rate](guides/exposure) | Guide with reference code: checking that the brightness measurement reaches the CPU, and keeping eye adaptation at 30 fps speed at any frame rate | Tested on one NVIDIA machine; the guide lists what has not been verified |
| [Internal and output resolution](guides/resolution) | Guide with reference code: the scene at its own resolution and the interface at the screen's, shaders kept in the game's pixels, and sharper distance-field text | Tested on one NVIDIA machine; the guide lists what has not been verified |
| [Save protection and crash reports](guides/saves) | Guide with reference code: a journal that brings back the console's save commit, backups before each run, and crash reports that say what they contain | Tested on one Windows machine; the guide lists what has not been verified |
| [PC settings in the game's own menu](guides/settings) | Guide with reference code: extra pages in the game's own Options screen without shipping game files, PC settings kept out of the save data, rows that say why they are greyed, and key rebinding | Tested on one Windows machine; the guide lists what has not been verified |
| [Translator: branch elimination](translator/branch-phi) | Patch and test for a bug in Ryujinx's shader translator that makes a conditional assignment unconditional | Tested at translator level |

## Ground rules

- Nothing that came out of a game is in this repository: no game files, keys, recompiled code, shaders or assets.
- Results say how they were obtained and what was not tested. We have no AMD device, so nothing here claims to fix an AMD problem.

## Licence

MIT, see [LICENSE](LICENSE), unless a file says otherwise. Third-party material is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
