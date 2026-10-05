"""Offline disassembly and cross-references for the supported local executable.

No process access. Candidate relative references are decoded from their unwind
function boundary before being reported, to discard operand-byte false matches.
Capstone 5.0.6 is installed locally in third_party/python for research only.
"""
import argparse
import bisect
import hashlib
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'third_party/python'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from inspect_game import PE, EXPECTED_SHA256

GAME = pathlib.Path(r"C:\Program Files (x86)\Steam\steamapps\common\Marvel's Spider-Man Remastered\Spider-Man.exe")


class Research:
    def __init__(self, path=GAME):
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
            raise ValueError('Unsupported executable hash')
        self.pe = PE(data)
        pe_offset = self.pe.u32(0x3c)
        rva, size = struct.unpack_from('<II', data, pe_offset + 24 + 112 + 3 * 8)
        self.functions = list(struct.iter_unpack('<III', self.pe.bytes(rva, size)))
        self.starts = [x[0] for x in self.functions]
        self.decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        self.cache = {}

    def bounds(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and rva < self.functions[i][1]:
            return self.functions[i][:2]
        return None

    def disassemble(self, rva, length=None):
        bounds = self.bounds(rva)
        if length is None and not bounds:
            raise ValueError(f'No unwind function covers {rva:#x}; supply --length for a leaf function')
        start, end = (rva, rva + length) if length else bounds
        if (start, end) not in self.cache:
            self.cache[start, end] = list(self.decoder.disasm_lite(self.pe.bytes(start, end-start), start))
        return self.cache[start, end]

    def references(self, targets):
        # Direct rel32 calls/jumps and common RIP-relative load/LEA encodings.
        pattern = re.compile(rb'[\xe8\xe9][\s\S]{4}|[\x40-\x4f]?[\x8b\x89\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d][\s\S]{4}')
        seen = set()
        for section in self.pe.sections:
            if not section['flags'] & 0x20000000:
                continue
            data = self.pe.data[section['raw']:section['raw']+section['raw_size']]
            # Lookahead permits overlapping candidates; verified below.
            for match in re.finditer(b'(?=(' + pattern.pattern + b'))', data):
                raw = match.group(1)
                source = section['rva'] + match.start()
                target = source + len(raw) + struct.unpack_from('<i', raw, len(raw)-4)[0]
                if target not in targets or source in seen:
                    continue
                bounds = self.bounds(source)
                if not bounds:
                    continue
                for address, size, mnemonic, operands in self.disassemble(source):
                    if address == source and size == len(raw):
                        seen.add(source)
                        yield dict(source=hex(source), target=hex(target), function=hex(bounds[0]),
                                   instruction=f'{mnemonic} {operands}')
                        break

    def strings(self, expression):
        pattern = re.compile(expression, re.I)
        for section in self.pe.sections:
            if section['flags'] & 0x20000000:
                continue
            data = self.pe.data[section['raw']:section['raw']+section['raw_size']]
            for match in re.finditer(rb'[\x20-\x7e]{5,512}\x00', data):
                value = match.group()[:-1].decode('ascii')
                if pattern.search(value):
                    yield section['rva']+match.start(), value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['function', 'refs', 'strings'])
    parser.add_argument('values', nargs='+')
    parser.add_argument('--length', type=lambda x: int(x, 0))
    args = parser.parse_args()
    research = Research()
    if args.mode == 'function':
        for value in args.values:
            for address, size, mnemonic, operands in research.disassemble(int(value, 0), args.length):
                print(f'{address:08x}  {mnemonic:8} {operands}')
    elif args.mode == 'refs':
        for reference in research.references({int(x, 0) for x in args.values}):
            print(reference)
    else:
        for rva, value in research.strings('|'.join(args.values)):
            print(f'{rva:08x} {value}')


if __name__ == '__main__':
    main()
