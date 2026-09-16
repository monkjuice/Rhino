#!/usr/bin/env python3
"""Write Forge's factory wavetables.

A wavetable file is an ordinary .wav holding single-cycle frames end to end, so
these open in Serum, Vital and Bitwig as readily as they open in Forge. Each
frame is 2048 samples of 32-bit float, bipolar, scaled so its peak reaches full
scale; the frame size is named in a `clm ` chunk the way Serum names its own,
and Forge reads that chunk to decide where one frame ends and the next begins.

Sixteen frames each. That is enough for POSITION to sweep through rather than
step across, and few enough that every frame is still legible in the editor's
frame strip and that the whole table fits comfortably inside a preset.

Run from anywhere; the files land beside this script.

    python instruments/theta-forge/tables/make-tables.py
"""

import math
import os
import struct

FRAME_SIZE = 2048
FRAMES = 16
SAMPLE_RATE = 44100

TWO_PI = 2.0 * math.pi


def normalised(frame):
    """Bipolar and reaching full scale, so morphing between frames of one table
    never changes how loud the oscillator is."""
    peak = max(abs(x) for x in frame)
    if peak < 1e-9:
        return frame
    return [x / peak for x in frame]


def phases():
    return [i / FRAME_SIZE for i in range(FRAME_SIZE)]


def morph_amount(frame_index):
    """0 at the first frame, 1 at the last."""
    return frame_index / (FRAMES - 1)


# --- The ten -----------------------------------------------------------------
#
# Each is a function of (phase, morph) returning one sample. They are written as
# formulas for the same reason Forge's built-in ten are: a formula is how a
# frame is authored, and the samples are what everything downstream reads.


def basic(p, m):
    """Sine to triangle to square to saw: the four shapes every other table is
    heard against, in the order they gain harmonics."""
    sine = math.sin(TWO_PI * p)
    triangle = 1.0 - 4.0 * abs(p - 0.5)
    square = 1.0 if p < 0.5 else -1.0
    saw = p * 2.0 if p < 0.5 else p * 2.0 - 2.0
    stages = [sine, triangle, square, saw]
    position = m * (len(stages) - 1)
    first = min(len(stages) - 2, int(position))
    blend = position - first
    return stages[first] * (1.0 - blend) + stages[first + 1] * blend


def pwm(p, m):
    """A pulse closing from a square to a sliver. The even harmonics arrive as
    it narrows, which is the sound a PWM LFO is reaching for."""
    width = 0.5 - 0.46 * m
    return 1.0 if p < width else -1.0


def harmonics(p, m):
    """A saw built up one harmonic at a time: a sine at the first frame and a
    band-limited saw of thirty-two harmonics at the last."""
    count = 1 + int(round(m * 31))
    total = 0.0
    for harmonic in range(1, count + 1):
        total += math.sin(TWO_PI * harmonic * p) / harmonic
    return total


def fold(p, m):
    """A sine driven into a wavefolder. Every extra fold is another pair of odd
    harmonics, so it brightens without ever reaching an edge."""
    drive = 1.0 + m * 6.0
    return math.sin(TWO_PI * p * 0.5 + math.sin(TWO_PI * p) * drive)


def sync(p, m):
    """Hard sync: an oscillator running up to five times the cycle rate, cut off
    and restarted at every cycle boundary. The discontinuity that leaves is the
    whole sound."""
    ratio = 1.0 + m * 4.0
    return math.sin(TWO_PI * (p * ratio % 1.0))


def formant(p, m):
    """A burst of a rising harmonic under a raised cosine — the shape a vowel
    makes, sweeping from a back vowel to a front one."""
    harmonic = 2.0 + m * 9.0
    window = 0.5 - 0.5 * math.cos(TWO_PI * p)
    return math.sin(TWO_PI * harmonic * p) * window


def bell(p, m):
    """Partials that slide off the harmonic series as the table is swept, so it
    goes from an organ to something struck."""
    total = math.sin(TWO_PI * p)
    for index, weight in enumerate([0.6, 0.4, 0.25], start=2):
        stretched = index * (1.0 + m * 0.18 * index)
        total += weight * math.sin(TWO_PI * stretched * p)
    return total


def digital(p, m):
    """A sine quantised to fewer and fewer steps: thirty-two at the first frame
    and three at the last."""
    steps = max(3, int(round(32 - m * 29)))
    value = math.sin(TWO_PI * p)
    return math.floor(value * steps + 0.5) / steps


def comb(p, m):
    """A saw against a delayed copy of itself, the delay opening as the table is
    swept. The notches it puts through the spectrum move with it."""
    saw = lambda x: 2.0 * (x % 1.0) - 1.0
    delay = 0.02 + m * 0.46
    return saw(p) - saw(p + delay)


def organ(p, m):
    """Drawbars: a fundamental with an octave, a twelfth and a fifteenth over
    it, the upper bars pulled out as the table is swept."""
    upper = m
    return (math.sin(TWO_PI * p)
            + 0.5 * upper * math.sin(2.0 * TWO_PI * p)
            + 0.45 * upper * math.sin(3.0 * TWO_PI * p)
            + 0.3 * upper * upper * math.sin(4.0 * TWO_PI * p))


TABLES = [
    ("basic-shapes", basic),
    ("pulse-width", pwm),
    ("harmonic-stack", harmonics),
    ("wavefolder", fold),
    ("hard-sync", sync),
    ("formant-sweep", formant),
    ("bell-partials", bell),
    ("bit-crush", digital),
    ("comb-notch", comb),
    ("drawbars", organ),
]


def write_wav(path, samples):
    """RIFF / fmt / clm / data, 32-bit float mono.

    The `clm ` chunk is written before the data the way Serum writes it, and its
    text is the marker, the frame size, and a field every reader ignores. A
    reader that does not know the chunk skips it on its length, which is why
    putting it here breaks nothing."""
    audio = b''.join(struct.pack('<f', s) for s in samples)
    fmt = struct.pack('<HHIIHH', 3, 1, SAMPLE_RATE, SAMPLE_RATE * 4, 4, 32)
    clm = ('<!>%04d 00000000 wavetable (theta forge)' % FRAME_SIZE).encode('ascii')
    if len(clm) % 2:
        clm += b'\x00'

    chunks = (b'fmt ' + struct.pack('<I', len(fmt)) + fmt
              + b'clm ' + struct.pack('<I', len(clm)) + clm
              + b'data' + struct.pack('<I', len(audio)) + audio)
    with open(path, 'wb') as out:
        out.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    grid = phases()
    for name, shape in TABLES:
        samples = []
        for frame in range(FRAMES):
            morph = morph_amount(frame)
            samples.extend(normalised([shape(p, morph) for p in grid]))
        path = os.path.join(here, name + '.wav')
        write_wav(path, samples)
        print('%-16s %d frames  %d bytes' % (name, FRAMES, os.path.getsize(path)))


if __name__ == '__main__':
    main()
