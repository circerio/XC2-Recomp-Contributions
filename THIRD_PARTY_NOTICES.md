# Third-party notices

| Where | What | Licence |
|---|---|---|
| `translator/branch-phi/0010-keep-branch-when-target-phis-differ.patch` | The diff's context lines are from `BranchElimination.cs` in Ryujinx's shader translator | MIT, Ryujinx Team and Contributors |

No other third-party code is included. The instruction encodings in `translator/branch-phi/make_repro.py` are facts about the Maxwell instruction set,
read from Ryujinx's decoder table.

## Ideas learned from other projects

No code was copied for these.

| Where | Idea | Source |
|---|---|---|
| `guides/hdr` (`roll_off_largest`, and the blend in `roll_off`) | Rolling off near the peak on the largest channel, so that the hue is kept; blending two results by a strength, as its `hdr_hue` setting does. Setting that strength from the room above paper white is ours | [xcx-recomp PR #103](https://github.com/jmars125/xcx-recomp/pull/103) by jmars125 (MIT). Its tone map credits its approach to TotK Recomp's and, through it, to RenoDX's TotK mod by souperman9 and Musa |
| `guides/hdr` (the guide's section 3, text only) | Moving a result's hue back toward a reference while keeping its lightness and chroma, one of the approaches the guide compares | [RenoDX](https://github.com/clshortfuse/renodx) (MIT, Carlos Lopez Jr.) |
