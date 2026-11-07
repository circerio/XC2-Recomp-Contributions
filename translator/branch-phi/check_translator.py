"""Builds a checkout's shader translator (xc2-recomp or xcx-recomp) as a plain .NET program, and runs the minimal repro
through it in debug mode and outside it. Needs no game files, keys or decryption tools, and does not change the checkout.

  python check_translator.py --checkout <repo> --work <empty folder> [--patch <file>] [--expect kept|lost]
                             [--dotnet <dotnet>] [--spirv-dis <spirv-dis>] [--spirv-val <spirv-val>]
                             [--nuget-source <folder>]

The steps are those of the project's tools/build_xlate.py (copy the vendored Ryujinx, git apply tools/xlate/patches/*.patch),
except that it builds with `dotnet build` instead of NativeAOT, which needs no C++ tools. --patch applies one more patch
after the project's own. The result is read from the SPIR-V disassembly: the conditional is "kept" if the module still has
an OpBranchConditional and a float comparison. Exit 1 if the result outside debug mode is not --expect, or if debug mode
did not keep the conditional.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_repro import repro  # noqa: E402


def run(cmd, **kw):
    print('>', ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], **kw)


def prepare(checkout, work, extra_patch):
    src = work / 'ryujinx'
    shutil.copytree(checkout / 'tools' / 'vendor' / 'ryujinx', src)
    for junk in list(src.glob('src/*/obj')) + list(src.glob('src/*/bin')):
        shutil.rmtree(junk)
    patches = sorted((checkout / 'tools' / 'xlate' / 'patches').glob('*.patch'))
    if extra_patch:
        patches.append(extra_patch)
    for patch in patches:
        r = run(['git', 'apply', '--whitespace=nowarn', '--directory', 'ryujinx', patch], cwd=work, capture_output=True,
                text=True)
        if r.returncode:
            sys.exit(f'patch does not apply: {patch.name}\n{r.stdout}{r.stderr}')
    print(f'applied {len(patches)} patches', flush=True)
    xlate = work / 'xlate'
    xlate.mkdir()
    for f in (checkout / 'tools' / 'xlate').iterdir():
        if f.is_file():
            shutil.copy2(f, xlate / f.name)
    return src, next(xlate.glob('*.csproj'))


def build(a, src, csproj, work):
    out = work / 'bin'
    cmd = [a.dotnet, 'build', csproj, '-c', 'Release', f'-p:RyuSrc={src}', '-p:InvariantGlobalization=true', '-o', out,
           '-m:2', '-nodeReuse:false', '-v:minimal']
    if a.nuget_source:
        cmd += ['--source', a.nuget_source]
    if run(cmd).returncode:
        sys.exit('dotnet build failed')
    return out / (csproj.stem + '.dll')


def translate(a, dll, code, out, debug):
    key = 'repro_debug' if debug else 'repro'
    job = {'key': key, 'stage': 'ps', 'code': str(code), 'debug': debug, 'fout': {'0': 'float'}}
    line = json.dumps({'job': job, 'out': str(out)}) + '\n'
    r = run([a.dotnet, dll, '--serve'], input=line, capture_output=True, text=True, timeout=120)
    if r.stdout.strip() != f'ok {key}':
        sys.exit(f'translator replied {r.stdout!r}; stderr {r.stderr}')
    return out / f'{key}.spv'


def verdict(a, spv):
    text = run([a.spirv_dis, spv], capture_output=True, text=True).stdout
    lines = [ln.strip() for ln in text.splitlines()]
    branch = [ln for ln in lines if 'OpBranchConditional' in ln]
    compare = [ln for ln in lines if ' OpFOrd' in ln or ' OpFUnord' in ln]
    if a.spirv_val:
        v = run([a.spirv_val, '--target-env', 'vulkan1.3', spv], capture_output=True, text=True)
        print(f'spirv-val exit {v.returncode} {v.stdout}{v.stderr}'.strip())
    print(f'{spv.name}: {len(branch)} OpBranchConditional, {len(compare)} float comparisons')
    return 'kept' if branch and compare else 'lost'


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--checkout', type=Path, required=True)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--patch', type=Path)
    ap.add_argument('--expect', choices=('kept', 'lost'))
    ap.add_argument('--dotnet', default=shutil.which('dotnet') or 'dotnet')
    ap.add_argument('--spirv-dis', default=shutil.which('spirv-dis') or 'spirv-dis')
    ap.add_argument('--spirv-val', default=shutil.which('spirv-val'))
    ap.add_argument('--nuget-source')
    a = ap.parse_args()
    work = a.work.resolve()
    if work.exists() and any(work.iterdir()):
        sys.exit(f'{work} is not empty')
    work.mkdir(parents=True, exist_ok=True)
    src, csproj = prepare(a.checkout.resolve(), work, a.patch.resolve() if a.patch else None)
    dll = build(a, src, csproj, work)
    code = work / 'repro.bin'
    code.write_bytes(repro())
    out = work / 'out'
    out.mkdir()
    debug = verdict(a, translate(a, dll, code, out, True))
    normal = verdict(a, translate(a, dll, code, out, False))
    print(f'RESULT debug mode: conditional {debug}; outside debug mode: conditional {normal}', flush=True)
    failed = debug != 'kept' or (a.expect and normal != a.expect)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
