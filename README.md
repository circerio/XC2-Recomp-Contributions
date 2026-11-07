# XC2-Recomp Contributions

Technical notes, small tools and patches from the work on [XC2-Recomp](https://github.com/circerio/XC2-Recomp), shared for other people
porting Nintendo Switch games to PC by static recompilation. Everything here builds and reads on its own: you do not need XC2-Recomp's
source, which is not public, and you do not need any game files.

This work stands on what other people have published. In particular, **Ryujinx** (`Ryujinx.Graphics.Shader`),
[**SuperMarioWonderRecomp**](https://github.com/BluetufieBugado/SuperMarioWonderRecomp), [**AksharDP's xc2-recomp**](https://github.com/AksharDP/xc2-recomp),
[**jmars125's xcx-recomp**](https://github.com/jmars125/xcx-recomp) and **Monado** are the projects these notes most often refer to. Where something here builds on someone else's work, the page says so, and credit for those parts stays with them.

## Contents

| Topic | What it is | Status |
|---|---|---|
| [Variable refresh rate](guides/vrr) | Guide with reference code: present modes, the frame rate cap, verifying VRR, and a refresh meter that jumps at a locked frame rate | Tested on one NVIDIA machine; the part on the jumping meter is still waiting on verification |
| [HDR output](guides/hdr) | Guide with reference code: HDR10 and scRGB from a game made for SDR, peak brightness and paper white, keeping highlights and their hue | Tested on one NVIDIA machine; the guide lists what has not been verified |
| [Translator: branch elimination](translator/branch-phi) | Patch and test for a bug in Ryujinx's shader translator that makes a conditional assignment unconditional | Tested at translator level |

## Ground rules

- Nothing that came out of a game is in this repository: no game files, keys, recompiled code, shaders or assets.
- Results say how they were obtained and what was not tested. We have no AMD device, so nothing here claims to fix an AMD problem.

## Licence

MIT, see [LICENSE](LICENSE), unless a file says otherwise. Third-party material is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
