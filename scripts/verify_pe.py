#!/usr/bin/env python3
"""Check the plugin's PE contract without executing Windows code or needing an SDK."""

import argparse
from pathlib import Path
import struct
import sys


class InvalidPE(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidPE(message)


def verify(path):
    data = Path(path).read_bytes()

    def unpack(fmt, offset):
        size = struct.calcsize(fmt)
        require(0 <= offset <= len(data) - size, "truncated PE structure")
        return struct.unpack_from(fmt, data, offset)

    require(data[:2] == b"MZ", "missing DOS header")
    pe, = unpack("<I", 0x3C)
    require(data[pe:pe + 4] == b"PE\0\0", "missing PE signature")
    machine, count, timestamp, _, _, opt_size, flags = unpack("<HHIIIHH", pe + 4)
    require(machine == 0x8664, "expected AMD64 machine")
    require(flags & 0x2002 == 0x2002, "expected an executable DLL")
    require(timestamp == 0, "PE timestamp must be zero")
    opt = pe + 24
    require(opt_size >= 112, "truncated PE32+ optional header")
    magic, = unpack("<H", opt)
    require(magic == 0x20B, "expected PE32+")
    entry, = unpack("<I", opt + 16)
    subsystem, = unpack("<H", opt + 68)
    require(subsystem == 2, "expected Windows GUI subsystem")
    directory_count, = unpack("<I", opt + 108)
    require(14 <= directory_count <= (opt_size - 112) // 8, "invalid PE directory count")

    sections = []
    require(0 < count <= 96, "invalid section count")
    for index in range(count):
        section = opt + opt_size + index * 40
        _, virtual, raw_size, raw = unpack("<IIII", section + 8)
        characteristics, = unpack("<I", section + 36)
        require(raw_size == 0 or raw + raw_size <= len(data), "truncated section")
        sections.append((virtual, raw_size, raw, characteristics))

    def locate(rva, size=1):
        for virtual, raw_size, raw, _ in sections:
            delta = rva - virtual
            if 0 <= delta and delta + size <= raw_size:
                return raw + delta, raw + raw_size
        raise InvalidPE(f"unmapped RVA: {rva:#x}")

    def at(fmt, rva):
        offset, _ = locate(rva, struct.calcsize(fmt))
        return unpack(fmt, offset)

    def string(rva):
        offset, end = locate(rva)
        terminator = data.find(b"\0", offset, end)
        require(terminator != -1, "unterminated PE name")
        return data[offset:terminator].decode("ascii")

    def directory(index):
        return unpack("<II", opt + 112 + index * 8)

    def executable(rva):
        return any(virtual <= rva < virtual + size and flags & 0x20000000
                   for virtual, size, _, flags in sections)

    require(entry != 0 and executable(entry), "entry point must be in executable code")
    imports_rva, imports_size = directory(1)
    require(imports_rva != 0 and imports_size >= 40, "missing import directory")
    locate(imports_rva, imports_size)
    imports = []
    for offset in range(0, imports_size - 19, 20):
        descriptor = at("<IIIII", imports_rva + offset)
        if not any(descriptor):
            break
        require(descriptor[3] != 0, "missing import DLL name")
        imports.append(string(descriptor[3]).upper())
    else:
        raise InvalidPE("unterminated import directory")
    require(imports == ["KERNEL32.DLL"], f"unexpected import DLLs: {imports}")
    require(directory(13) == (0, 0), "unexpected delay imports")

    exports_rva, exports_size = directory(0)
    require(exports_rva != 0 and exports_size >= 40, "missing export directory")
    locate(exports_rva, exports_size)
    functions, names, function_table, name_table, ordinal_table = at("<IIIII", exports_rva + 20)
    require(functions == 2 and names == 2, "expected exactly two SKSE exports")
    exports = set()
    for index in range(names):
        name_rva, = at("<I", name_table + index * 4)
        exports.add(string(name_rva))
        ordinal, = at("<H", ordinal_table + index * 2)
        require(ordinal < functions, "invalid export ordinal")
        target, = at("<I", function_table + ordinal * 4)
        require(not exports_rva <= target < exports_rva + exports_size, "forwarded export")
        require(executable(target), "SKSE export must point to executable code")
    require(exports == {"SKSEPlugin_Query", "SKSEPlugin_Load"}, f"unexpected exports: {exports}")
    print(f"PE verification: PASS ({path})")
    print("AMD64 DLL; timestamp 0; KERNEL32.dll only; SKSEPlugin_Query + SKSEPlugin_Load")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll", type=Path)
    args = parser.parse_args()
    try:
        verify(args.dll)
    except (OSError, ValueError, struct.error) as error:
        print(f"PE verification: FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
