"""A minimal Maxwell fragment shader that shows the branch elimination bug. Hand encoded from the opcode patterns in
Ryujinx's Decoders/InstTable.cs; it contains no game data.

  python make_repro.py <out.bin>

The file is the shader program header followed by the code, from offset 0 (what Ryujinx.ShaderTools and the
xc2-recomp / xcx-recomp translators take). The program writes render target 0 from R0..R3 at EXIT:

    MOV32I R0, 0.0 ; MOV32I R1, 0.0 ; MOV32I R2, 0.0 ; MOV32I R3, 1.0
    FSETP.LE.AND P0, PT, RZ, c[3][0], PT       P0 = 0 <= c[3][0]
    SSY L
    @!P0 SYNC                                  P0 false: leave with R0 = 0.0
    MOV32I R0, 1.0                             P0 true: R0 = 1.0
    SYNC
  L: EXIT

Right: color.r = c[3][0] >= 0 ? 1.0 : 0.0. With the bug, outside debug mode: color.r = 1.0 always.
"""
import struct
import sys

ONE = 0x3f800000
RZ = 255
NOP = 0x50b0000000070f00


def mov32i(rd, bits):
    return 0x010 << 52 | (bits & 0xffffffff) << 20 | 7 << 16 | 0xf << 12 | rd


def fsetp_c_le(pd, ra, slot, word):  # FSETP.LE.AND Pd, PT, Ra, c[slot][word], PT
    return 0x4bb << 52 | 3 << 48 | 7 << 39 | slot << 34 | word << 20 | 7 << 16 | ra << 8 | pd << 3 | 7


def ssy(addr, target):
    return 0xe29 << 52 | ((target - addr - 8) & 0xffffff) << 20 | 7 << 16


def sync(pred=7, inv=False):
    return 0x1e1f << 51 | (1 << 19 if inv else 0) | pred << 16 | 0xf


def exit_():
    return 0xe30 << 52 | 7 << 16 | 0xf


def bra(addr, target):
    return 0xe24 << 52 | ((target - addr - 8) & 0xffffff) << 20 | 7 << 16 | 0xf


def program():
    code = {  # address after the header -> instruction; 0x00, 0x20, ... are scheduling words (0 here)
        0x08: mov32i(0, 0),
        0x10: mov32i(1, 0),
        0x18: mov32i(2, 0),
        0x28: mov32i(3, ONE),
        0x30: fsetp_c_le(0, RZ, 3, 0),
        0x38: ssy(0x38, 0x68),
        0x48: sync(0, inv=True),
        0x50: mov32i(0, ONE),
        0x58: sync(),
        0x68: exit_(),
        0x70: bra(0x70, 0x70),
    }
    out = bytearray()
    for addr in range(0, 0x80, 8):
        out += struct.pack('<Q', code.get(addr, 0 if addr & 0x1f == 0 else NOP))
    return bytes(out)


def header():
    words = [0] * 20
    words[0] = 2 | 3 << 5 | 5 << 10 | 3 << 17  # type 2, version 3, fragment, SASS version 3
    words[18] = 0xf  # render target 0: RGBA from R0..R3
    return struct.pack('<20I', *words)


def repro():
    return header() + program()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], 'wb') as f:
        f.write(repro())
