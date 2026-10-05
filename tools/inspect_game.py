"""Offline, read-only inspection. Does not attach, inject, patch, or launch the game."""
import argparse
import hashlib
import json
import pathlib
import struct
import sys

EXPECTED_SHA256 = "e297d4d94f1ffe4febf289745e79e7b6fa233a788e7a00f480fc77c55db81ad1"
# Research leads from ArkWeb 17ec697bd431fce96a60fff1075f7e399d296058.
# These are byte/address facts, independently checked against the local PE.
# A match does NOT establish a valid calling convention or render hook.
FUNCTIONS = {
    "camera_update": (0x897D30, "48 8b c4 48 89 58 10 44 88 48 20 44 88 40 18 55"),
    "havok_cast_ray": (0x2E67010, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18"),
    "havok_pre_collide": (0x2E54300, "41 56 48 83 ec 50 48 89 5c 24 60 48 89 6c 24 68"),
}
VTABLES = {"hero_local": 0x38A93C8, "hero_camera_manager": 0x38B1DD0}

class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ": raise ValueError("Not a PE executable")
        pe = self.u32(0x3C)
        if data[pe:pe + 4] != b"PE\0\0": raise ValueError("Invalid PE signature")
        self.machine, count = struct.unpack_from("<HH", data, pe + 4)
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        opt = pe + 24
        if struct.unpack_from("<H", data, opt)[0] != 0x20B: raise ValueError("Expected PE32+")
        self.image_base = self.u64(opt + 24)
        self.image_size = self.u32(opt + 56)
        self.sections = []
        for i in range(count):
            o = opt + optional_size + 40 * i
            name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
            virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, o + 8)
            self.sections.append(dict(name=name, rva=rva, virtual_size=virtual_size, raw=raw,
                                      raw_size=raw_size, flags=self.u32(o + 36)))

    def u32(self, offset): return struct.unpack_from("<I", self.data, offset)[0]
    def u64(self, offset): return struct.unpack_from("<Q", self.data, offset)[0]
    def offset(self, rva, size=1):
        for s in self.sections:
            relative = rva - s["rva"]
            if 0 <= relative and relative + size <= s["raw_size"]:
                o = s["raw"] + relative
                if o + size <= len(self.data): return o
        raise ValueError(f"RVA {rva:#x} not backed by file")
    def bytes(self, rva, size):
        o = self.offset(rva, size)
        return self.data[o:o + size]
    def executable(self, rva):
        return any(s["rva"] <= rva < s["rva"] + s["virtual_size"] and s["flags"] & 0x20000000
                   for s in self.sections)
    def vtable(self, rva):
        locator = self.u64(self.offset(rva - 8, 8)) - self.image_base
        col = self.offset(locator, 24)
        signature, _, _, descriptor, _, self_rva = struct.unpack_from("<IIIIII", self.data, col)
        if signature != 1 or self_rva != locator: raise ValueError("Invalid MSVC RTTI locator")
        name_at = self.offset(descriptor + 16)
        end = self.data.index(b"\0", name_at, min(name_at + 512, len(self.data)))
        first = self.u64(self.offset(rva, 8)) - self.image_base
        return {"type": self.data[name_at:end].decode("ascii"), "first_function_rva": hex(first),
                "first_function_executable": self.executable(first)}

def inspect(executable):
    data = executable.read_bytes()
    pe = PE(data)
    digest = hashlib.sha256(data).hexdigest()
    functions = {}
    for name, (rva, pattern) in FUNCTIONS.items():
        expected = bytes.fromhex(pattern)
        actual = pe.bytes(rva, len(expected))
        functions[name] = dict(rva=hex(rva), matches=actual == expected,
                               executable=pe.executable(rva), actual=actual.hex(" "))
    tables = {}
    for name, rva in VTABLES.items():
        try: tables[name] = dict(rva=hex(rva), **pe.vtable(rva))
        except (ValueError, struct.error) as error: tables[name] = {"rva": hex(rva), "error": str(error)}
    existing = [p.name for p in executable.parent.iterdir()
                if p.name.lower() in {"dxgi.dll", "dinput8.dll", "winmm.dll", "version.dll", "realvr.ini", "openvr_api.dll"}]
    return dict(executable=str(executable), sha256=digest, known_executable=digest == EXPECTED_SHA256,
                machine=hex(pe.machine), image_base=hex(pe.image_base), image_size=pe.image_size,
                candidate_functions=functions, candidate_vtables=tables, existing_mod_files=existing,
                game_adapter_ready=False, native_game_stereo_verified=False,
                note="Offline signature and RTTI checks only. No runtime ABI or stereo render path established.")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    try: result = inspect(args.executable)
    except (OSError, ValueError, struct.error) as error:
        print(f"Inspection failed: {error}", file=sys.stderr); return 2
    output = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)
    return 0 if result["known_executable"] else 1

if __name__ == "__main__": sys.exit(main())
