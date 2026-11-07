# Shader translator: branch elimination ignores phi nodes

A bug in `Translation/Optimizations/BranchElimination.cs` of Ryujinx's shader translator (`Ryujinx.Graphics.Shader`). Outside debug
mode, it can turn a conditional assignment into an unconditional one. The translator itself is the work of the Ryujinx
team and contributors (MIT); this folder only adds the patch, a test and this note.

## The bug

The pass removes a block's branch when the next block holds only an unconditional branch to the same target. It runs after
`Ssa.Rename`, so the target's phi nodes take one value per predecessor. If the current block and the next block bring
different values into a phi, removing the branch makes the current block fall through the next one, and the next block's value
always wins. Maxwell code like this:

```
SSY L
@!P0 SYNC          ; P0 false: leave with R0 unchanged
MOV32I R0, 1.0     ; P0 true: R0 = 1.0
SYNC
L: ...
```

is translated as an unconditional `R0 = 1.0`, and the comparison that set `P0`, with its constant buffer read, disappears.

Debug mode does not show it: `Translator.EmitOps` then adds a comment node before every instruction, so the next block is
never "only a branch" and the pass does not fire. A project that translates outside debug mode gets the wrong code.

## The fix

[`0010-keep-branch-when-target-phis-differ.patch`](0010-keep-branch-when-target-phis-differ.patch) keeps the branch unless every phi in
the target gets the same value from both blocks. The name and path format follow the patch series in `tools/xlate/patches/` of
xc2-recomp and xcx-recomp, and it applies to their vendored Ryujinx with `git apply --directory`.

## Test

[`make_repro.py`](make_repro.py) writes a 208-byte fragment shader, hand encoded from the opcode patterns in `Decoders/InstTable.cs`.
It contains no game data. [`check_translator.py`](check_translator.py) builds a checkout's translator from its own vendored Ryujinx and patches,
translates that shader in debug mode and outside it, and looks for the conditional in the SPIR-V. It needs the .NET 10 SDK and
`spirv-dis`; no game files, keys or decryption tools.

```sh
python check_translator.py --checkout <xc2-recomp or xcx-recomp> --work <empty folder> --expect lost
python check_translator.py --checkout <xc2-recomp or xcx-recomp> --work <empty folder> --expect kept \
    --patch 0010-keep-branch-when-target-phis-differ.patch
```

Results on 2026-11-08, with a plain `dotnet build` rather than the projects' NativeAOT publish:

| Project | Commit | Patch | Debug mode | Outside debug mode (what the runtime asks for) |
|---|---|---|---|---|
| xc2-recomp | `b6c6f49` | none | conditional kept | **conditional lost** |
| xc2-recomp | `b6c6f49` | this one | conditional kept | conditional kept |
| xcx-recomp | `7b2337a` | none | conditional kept | **conditional lost** |
| xcx-recomp | `7b2337a` | this one | conditional kept | conditional kept |

Both runtimes send `"debug":false` with every job (`runtime/gfx_shader.c`).

## Where it showed up for us

In Xenoblade Chronicles 2, a cloud sea fragment shader takes a separate path when the camera is below the sea's top. The Maxwell
code sets its starting coverage to 1.0 only if a further condition holds (the camera is well inside the sea). The translated shader
set it to 1.0 every time, so with the camera slightly under the top, at about −8.1 to −8.5 against a top of −8, every ray started fully covered and the
sky turned into a flat fog colour.

With the patch in our own port, those shots matched recordings of the same moments on a console, including the thin fog that the path is meant to draw,
and our full regression stayed green.

We also translated all 11,418 distinct code blocks of the game's update 3.0.0 and its add-on content through xc2-recomp's translator, without and with the patch, outside
debug mode. The jobs carried no texture or attribute types, which is enough to compare two builds. 19 modules changed, each gaining one conditional branch, and all 19 pass `spirv-val`:

- **15 cloud shaders.** Our own translator changes the same number of cloud shaders, and each ends up with the same number of conditional branches as our build.
- **4 lighting shaders.** Our own translator does not change these, because it falls back to debug mode for them, where the pass does not fire. So we have never seen them on screen in
  this form, and with the patch they still have 2 conditional branches fewer than in debug mode. We have not looked into whether those 2 are harmless.

## What this does not claim

- It was not run inside xc2-recomp's or xcx-recomp's runtime. The results above are translator output only.
- It says nothing about the AMD-only cloud problems: this bug is a logic error in the translated code, the same on every GPU. We have no AMD device to test on.
- Whether it affects a given game depends on whether its shaders have this pattern. We only know about the one game.
- The emulators built on this translator are not affected as far as we can tell, because they translate in debug mode.

## Sources

The patch and the test are original work under this repository's MIT licence. The patch's context lines are Ryujinx code (MIT, Ryujinx Team and Contributors).
The instruction encodings in `make_repro.py` are facts taken from Ryujinx's decoder table.
