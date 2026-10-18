#!/usr/bin/env python3
"""Builds an FM Towns test disc from the boot record and a payload.

Writes <name>.bin with <name>.cue: one MODE1/2352 data track of raw sectors with their sync, header, EDC and ECC,
so any burner writes the disc as it is. The boot record fills sector 0 and the payload starts at sector 1.
"""

import os
import struct
import sys

USER_SIZE = 2048
RAW_SIZE = 2352
MIN_SECTORS = 300
PREGAP_FRAMES = 150
IPL_PAYLOAD_SECTORS_OFFSET = 0x40
PAYLOAD_LIMIT = 0x30000
IPL_LIMIT = 1024


def make_tables():
    forward = [0] * 256
    backward = [0] * 256
    edc = [0] * 256

    for i in range(256):
        j = ((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF
        forward[i] = j
        backward[i ^ j] = i
        value = i

        for _ in range(8):
            value = (value >> 1) ^ (0xD8018001 if value & 1 else 0)

        edc[i] = value

    return forward, backward, edc


FORWARD, BACKWARD, EDC = make_tables()


def edc_compute(data):
    value = 0

    for byte in data:
        value = (value >> 8) ^ EDC[(value ^ byte) & 0xFF]

    return value


def ecc_compute(sector, major_count, minor_count, major_mult, minor_inc, destination):
    source = sector[0x0C:]
    size = major_count * minor_count

    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        ecc_a = 0
        ecc_b = 0

        for _ in range(minor_count):
            value = source[index]
            index += minor_inc

            if index >= size:
                index -= size

            ecc_a ^= value
            ecc_b ^= value
            ecc_a = FORWARD[ecc_a]

        ecc_a = BACKWARD[FORWARD[ecc_a] ^ ecc_b]
        sector[destination + major] = ecc_a
        sector[destination + major + major_count] = ecc_a ^ ecc_b


def bcd(value):
    return ((value // 10) << 4) | (value % 10)


def raw_sector(lba, user):
    sector = bytearray(RAW_SIZE)
    sector[0:12] = b"\x00" + b"\xff" * 10 + b"\x00"
    frames = lba + PREGAP_FRAMES
    sector[12] = bcd(frames // (60 * 75))
    sector[13] = bcd((frames // 75) % 60)
    sector[14] = bcd(frames % 75)
    sector[15] = 1
    sector[16:16 + USER_SIZE] = user
    struct.pack_into("<I", sector, 0x810, edc_compute(sector[0:0x810]))
    ecc_compute(sector, 86, 24, 2, 86, 0x81C)
    ecc_compute(sector, 52, 43, 86, 88, 0x8C8)
    return bytes(sector)


def main():
    if len(sys.argv) != 4:
        print("usage: mkimage.py <ipl.bin> <payload.bin> <output name>")
        return 1

    with open(sys.argv[1], "rb") as f:
        ipl = f.read()

    with open(sys.argv[2], "rb") as f:
        payload = f.read()

    if len(ipl) > IPL_LIMIT:
        print("error: the boot record is %d bytes, more than %d" % (len(ipl), IPL_LIMIT))
        return 1

    if len(payload) > PAYLOAD_LIMIT:
        print("error: the payload is %d bytes, the limit is %d" % (len(payload), PAYLOAD_LIMIT))
        return 1

    payload_sectors = (len(payload) + USER_SIZE - 1) // USER_SIZE
    record = bytearray(ipl) + bytes(USER_SIZE - len(ipl))
    struct.pack_into("<H", record, IPL_PAYLOAD_SECTORS_OFFSET, payload_sectors)
    data = bytes(record) + payload
    count = max((len(data) + USER_SIZE - 1) // USER_SIZE, MIN_SECTORS)
    data += bytes(count * USER_SIZE - len(data))

    name = sys.argv[3]

    with open(name + ".bin", "wb") as f:
        for lba in range(count):
            f.write(raw_sector(lba, data[lba * USER_SIZE:(lba + 1) * USER_SIZE]))

    with open(name + ".cue", "w") as f:
        f.write('FILE "%s" BINARY\n' % os.path.basename(name + ".bin"))
        f.write("  TRACK 01 MODE1/2352\n")
        f.write("    INDEX 01 00:00:00\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
