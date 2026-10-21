#!/usr/bin/env python3
"""Builds an FM Towns test disc from the boot record and a payload.

Writes <name>.bin with <name>.cue: one MODE1/2352 data track of raw sectors with their sync, header, EDC and ECC,
so any burner writes the disc as it is. The boot record fills sector 0 and the payload starts at sector 1.

Options for discs that need more than that:
  --sectors N   the data track holds N sectors; the ones after the payload carry their own LBA (see tag_sector)
  --audio FILE  audio tracks after the data track, one line each: <raw file> <pregap frames> <index 00 frames>
                [PRE]. The pregap frames are left to the burner (PREGAP), the index 00 frames are the first frames
                of the raw file (16-bit little-endian stereo at 44.1 kHz), and PRE sets the pre-emphasis flag
"""

import operator
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
TAG_MAGIC = 0x33544D46
TAG_MIX = 0x5A5A5A5A


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


# Many sectors at once: each parity byte column is one byte lane of a big integer, multiplied by alpha lane by lane
class FastEcc:
    def __init__(self):
        self.backward = bytes(BACKWARD)
        self.q_gather = operator.itemgetter(*self.order(52, 43, 86, 88))

    @staticmethod
    def order(major_count, minor_count, major_mult, minor_inc):
        size = major_count * minor_count
        order = [0] * size

        for major in range(major_count):
            index = (major >> 1) * major_mult + (major & 1)

            for minor in range(minor_count):
                order[minor * major_count + major] = index
                index += minor_inc

                if index >= size:
                    index -= size

        return order

    @staticmethod
    def parity(rows, width, count, backward):
        high = int.from_bytes(b"\x80" * width, "little")
        low = int.from_bytes(b"\x7f" * width, "little")
        ecc_a = 0
        ecc_b = 0

        for minor in range(count):
            value = int.from_bytes(rows[minor * width:(minor + 1) * width], "little")
            ecc_a ^= value
            ecc_b ^= value
            ecc_a = ((ecc_a & low) << 1) ^ (((ecc_a & high) >> 7) * 0x1D)

        ecc_a = ((ecc_a & low) << 1) ^ (((ecc_a & high) >> 7) * 0x1D)
        first = (ecc_a ^ ecc_b).to_bytes(width, "little").translate(backward)
        second = (int.from_bytes(first, "little") ^ ecc_b).to_bytes(width, "little")
        return first + second

    def apply(self, sector):
        sector[0x81C:0x8C8] = self.parity(bytes(sector[0x0C:0x0C + 2064]), 86, 24, self.backward)
        sector[0x8C8:0x930] = self.parity(bytes(self.q_gather(sector[0x0C:0x0C + 2236])), 52, 43, self.backward)


FAST_ECC = FastEcc()


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
    FAST_ECC.apply(sector)
    return bytes(sector)


# A sector the tests can recognize by its data: the magic, the LBA, then dword i = (LBA << 9 | i) ^ 5A5A5A5Ah
def tag_sector(lba):
    words = [TAG_MAGIC, lba] + [((lba << 9) | i) ^ TAG_MIX for i in range(2, USER_SIZE // 4)]
    return struct.pack("<%dI" % len(words), *words)


def msf(frames):
    return "%02d:%02d:%02d" % (frames // 4500, (frames // 75) % 60, frames % 75)


def read_audio(path):
    tracks = []

    with open(path) as f:
        for line in f:
            fields = line.split()

            if not fields or fields[0].startswith("#"):
                continue

            raw = os.path.join(os.path.dirname(path), fields[0])
            tracks.append((raw, int(fields[1]), int(fields[2]), "PRE" in fields[3:]))

    return tracks


def main():
    arguments = sys.argv[1:]
    sectors = 0
    audio = []

    while len(arguments) > 3:
        if arguments[3] == "--sectors" and len(arguments) > 4:
            sectors = int(arguments[4])
        elif arguments[3] == "--audio" and len(arguments) > 4:
            audio = read_audio(arguments[4])
        else:
            break

        del arguments[3:5]

    if len(arguments) != 3:
        print("usage: mkimage.py <ipl.bin> <payload.bin> <output name> [--sectors N] [--audio FILE]")
        return 1

    with open(arguments[0], "rb") as f:
        ipl = f.read()

    with open(arguments[1], "rb") as f:
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

    name = arguments[2]

    if sectors != 0 and sectors < count:
        print("error: the payload needs %d sectors, more than the %d asked for" % (count, sectors))
        return 1

    cue = ['FILE "%s" BINARY' % os.path.basename(name + ".bin"), "  TRACK 01 MODE1/2352", "    INDEX 01 00:00:00"]

    with open(name + ".bin", "wb") as f:
        for lba in range(count):
            f.write(raw_sector(lba, data[lba * USER_SIZE:(lba + 1) * USER_SIZE]))

        for lba in range(count, sectors):
            f.write(raw_sector(lba, tag_sector(lba)))

        position = max(count, sectors)

        for number, (raw, pregap, index0, emphasis) in enumerate(audio, 2):
            with open(raw, "rb") as source:
                samples = source.read()

            frames = (len(samples) + RAW_SIZE - 1) // RAW_SIZE
            f.write(samples + bytes(frames * RAW_SIZE - len(samples)))
            cue.append("  TRACK %02d AUDIO" % number)

            if emphasis:
                cue.append("    FLAGS PRE")

            if pregap != 0:
                cue.append("    PREGAP %s" % msf(pregap))

            if index0 != 0:
                cue.append("    INDEX 00 %s" % msf(position))

            cue.append("    INDEX 01 %s" % msf(position + index0))
            position += frames

    with open(name + ".cue", "w") as f:
        f.write("\n".join(cue) + "\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())
