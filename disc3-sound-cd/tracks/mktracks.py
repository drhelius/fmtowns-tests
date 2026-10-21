#!/usr/bin/env python3
"""Writes disc 3's audio tracks as raw CD-DA (16-bit little-endian stereo at 44.1 kHz) and the track list that
mkimage.py --audio reads. Track 2 follows the data track, so its 2 s pregap is left to the burner; track 4 keeps
its 2 s INDEX 00 in the file; track 5 carries the pre-emphasis flag; track 7's right channel is 6 dB lower. cd.c expects this layout before it has the TOC.
"""

import array
import math
import os
import random
import sys

RATE = 44100
FULL = 32767
HALF = 0.5


def tone(frequency, seconds, level=HALF, phase=0.0):
    samples = []
    step = 2.0 * math.pi * frequency / RATE

    for i in range(int(seconds * RATE)):
        samples.append(level * math.sin(phase + step * i))

    return samples


def stepped_tone(first, step, seconds, level=HALF):
    samples = []
    phase = 0.0

    for second in range(seconds):
        increment = 2.0 * math.pi * (first + step * second) / RATE

        for _ in range(RATE):
            samples.append(level * math.sin(phase))
            phase += increment

    return samples


def dc(level, seconds):
    return [level] * int(seconds * RATE)


def square(frequency, seconds, level=HALF):
    half_period = RATE / (2.0 * frequency)
    return [level if int(i / half_period) % 2 == 0 else -level for i in range(int(seconds * RATE))]


# Paul Kellet's economy pink filter on a fixed seed, scaled to peaks near -6 dBFS
def pink(seconds, level=HALF):
    generator = random.Random(3)
    b0 = b1 = b2 = 0.0
    samples = []

    for _ in range(int(seconds * RATE)):
        white = generator.uniform(-1.0, 1.0)
        b0 = 0.99765 * b0 + white * 0.0990460
        b1 = 0.96300 * b1 + white * 0.2965164
        b2 = 0.57000 * b2 + white * 1.0526913
        samples.append((b0 + b1 + b2 + white * 0.1848) * 0.25)

    peak = max(abs(s) for s in samples)
    return [s * level / peak for s in samples]


def sweep(start, end, seconds, level=HALF):
    count = int(seconds * RATE)
    ratio = math.log(end / start)
    samples = []

    for i in range(count):
        t = i / RATE
        phase = 2.0 * math.pi * start * seconds / ratio * (math.exp(t / seconds * ratio) - 1.0)
        samples.append(level * math.sin(phase))

    return samples


# The right channel at right times the left one's level, so a recording tells the channels apart
def write(path, samples, right=1.0):
    data = array.array("h")

    for value in samples:
        sample = max(-32768, min(32767, int(round(value * FULL))))
        data.append(sample)
        data.append(max(-32768, min(32767, int(round(value * right * FULL)))))

    if sys.byteorder != "little":
        data.byteswap()

    with open(path, "wb") as f:
        f.write(data.tobytes())


def main():
    if len(sys.argv) != 2:
        print("usage: mktracks.py <output directory>")
        return 1

    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    tracks = [
        ("track02.raw", 150, 0, "", tone(1000, 10) + tone(100, 5) + tone(10000, 5)),
        ("track03.raw", 0, 0, "", (dc(HALF, 2) + dc(0.0, 2) + dc(-HALF, 2)) * 2 + square(5, 4) + square(10, 4) +
            square(20, 4) + square(40, 4)),
        ("track04.raw", 0, 150, "", tone(440, 2) + tone(880, 10)),
        ("track05.raw", 0, 0, "PRE", pink(5) + sweep(20, 20000, 10)),
        ("track06.raw", 0, 0, "", pink(5) + sweep(20, 20000, 10)),
        ("track07.raw", 0, 0, "", stepped_tone(300, 25, 60), 0.5),
    ]

    with open(os.path.join(out, "tracks.txt"), "w") as f:
        for track in tracks:
            name, pregap, index0, flags, samples = track[:5]
            write(os.path.join(out, name), samples, track[5] if len(track) > 5 else 1.0)
            f.write("%s %d %d %s\n" % (name, pregap, index0, flags))

    return 0


if __name__ == "__main__":
    sys.exit(main())
