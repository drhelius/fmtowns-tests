#!/usr/bin/env python3
"""Prints the result records of an FM Towns test disc.

records.py <file>...

A file is a runner's records dump, a RAM dump (the records are found by their magic), or the text zbarimg prints for
QR photos (one Base32 part per line, as `zbarimg --raw`); parts are put back together by their part header.
"""

import base64
import struct
import sys
import zlib

RECORD_MAGIC = b"FMTR"
HEADER = struct.Struct("<4sBBBBHBBIHHII")
STATUS = {0: "NONE", 1: "OK", 2: "FAULT", 3: "TIMEOUT", 4: "SKIP"}


def record_crc(data, length):
    header = bytearray(data[:HEADER.size])
    header[24:28] = b"\0\0\0\0"
    return zlib.crc32(bytes(header) + data[HEADER.size:HEADER.size + length]) & 0xFFFFFFFF


def parse(data):
    records = []
    position = 0

    while True:
        position = data.find(RECORD_MAGIC, position)

        if position < 0 or position + HEADER.size > len(data):
            break

        fields = HEADER.unpack_from(data, position)
        length = fields[10]
        end = position + HEADER.size + length

        if end <= len(data) and record_crc(data[position:end], length) == fields[12]:
            records.append((fields, data[position + HEADER.size:end]))
            position = end
        else:
            position += 4

    return records


def from_qr(text):
    parts = {}

    for line in text.splitlines():
        line = line.strip()

        if line.startswith("QR-Code:"):
            line = line[8:]

        if not line:
            continue

        padded = line + "=" * (-len(line) % 8)
        chunk = base64.b32decode(padded)

        if chunk[0] != 0x51:
            continue

        parts[chunk[3]] = (chunk[4], chunk[6:])

    if not parts:
        return b""

    count = max(total for total, _ in parts.values())
    missing = [i + 1 for i in range(count) if i not in parts]

    if missing:
        print("missing QR parts %s of %u" % (missing, count))

    return b"".join(parts[i][1] for i in sorted(parts))


def dump(payload):
    for offset in range(0, len(payload), 16):
        row = payload[offset:offset + 16]
        print("    %04X  %s" % (offset, " ".join("%02X" % b for b in row)))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            data = f.read()

        if RECORD_MAGIC not in data:
            data = from_qr(data.decode("ascii", "ignore"))

        for fields, payload in parse(data):
            _, disc, test, version, status, machine, time_base, leak, build, run, length, steps, _ = fields
            print("disc %u test %u version %u status %s machine %04X clock %s leak %u build %08X run %u steps %X "
                  "length %u" % (disc, test, version, STATUS.get(status, status), machine,
                                 "0026" if time_base == 0 else "PIT", leak, build, run, steps, length))
            dump(payload)

    return 0


if __name__ == "__main__":
    sys.exit(main())
