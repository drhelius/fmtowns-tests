#!/usr/bin/env python3
"""Builds FM Towns boot media from the boot record and a payload.

Writes <name>.img, a raw 2HD floppy image (77 cylinders, 2 heads, 8 sectors of 1024 bytes),
and <name>.iso with its <name>.cue, a single MODE1/2048 data track.
Both start with the boot record and carry the payload from the next sector on.
"""

import os
import struct
import sys

FLOPPY_SECTOR = 1024
FLOPPY_SIZE = 77 * 2 * 8 * FLOPPY_SECTOR
CD_SECTOR = 2048
CD_MIN_SECTORS = 300
IPL_PAYLOAD_SECTORS_OFFSET = 0x40
PAYLOAD_LIMIT = 0x30000


def sectors(size, sector_size):
    return (size + sector_size - 1) // sector_size


def boot_record(ipl, payload_sectors, sector_size):
    record = bytearray(ipl)
    struct.pack_into("<H", record, IPL_PAYLOAD_SECTORS_OFFSET, payload_sectors)
    return bytes(record) + bytes(sector_size - len(record))


def write_floppy(path, ipl, payload):
    count = sectors(len(payload), FLOPPY_SECTOR)
    image = bytearray(FLOPPY_SIZE)
    data = boot_record(ipl, count, FLOPPY_SECTOR) + payload
    image[:len(data)] = data

    with open(path, "wb") as f:
        f.write(image)


def write_cd(iso_path, cue_path, ipl, payload):
    count = sectors(len(payload), CD_SECTOR)
    data = boot_record(ipl, count, CD_SECTOR) + payload
    total = max(sectors(len(data), CD_SECTOR), CD_MIN_SECTORS)
    image = bytearray(total * CD_SECTOR)
    image[:len(data)] = data

    with open(iso_path, "wb") as f:
        f.write(image)

    with open(cue_path, "w") as f:
        f.write('FILE "%s" BINARY\n' % os.path.basename(iso_path))
        f.write("  TRACK 01 MODE1/2048\n")
        f.write("    INDEX 01 00:00:00\n")


def main():
    if len(sys.argv) != 4:
        print("usage: mkimage.py <ipl.bin> <payload.bin> <output name>")
        return 1

    with open(sys.argv[1], "rb") as f:
        ipl = f.read()

    with open(sys.argv[2], "rb") as f:
        payload = f.read()

    if len(ipl) > FLOPPY_SECTOR:
        print("error: the boot record is %d bytes, more than one floppy sector" % len(ipl))
        return 1

    if len(payload) > PAYLOAD_LIMIT:
        print("error: the payload is %d bytes, the limit is %d" % (len(payload), PAYLOAD_LIMIT))
        return 1

    name = sys.argv[3]
    write_floppy(name + ".img", ipl, payload)
    write_cd(name + ".iso", name + ".cue", ipl, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
