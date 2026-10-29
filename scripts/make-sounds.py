#!/usr/bin/env python3
"""Synthesises the aquarium's sounds (all made here, so no licensing questions).

Short, soft and low: they play on a desktop all day, so nothing should ever
be sharp or loud. Writes 16-bit mono WAVs into package/contents/sounds/.
"""
import math
import pathlib
import random
import struct
import wave

RATE = 44100
OUT = pathlib.Path(__file__).resolve().parent.parent / "package/contents/sounds"


def write(name, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(s / peak * 0.8 * 32767)) for s in samples))


def envelope(n, attack, decay):
    """Quick rise, exponential fall (both in seconds)."""
    a = max(1, int(attack * RATE))
    return [min(1.0, i / a) * math.exp(-i / (decay * RATE)) for i in range(n)]


def sweep(seconds, f0, f1, decay, attack=0.004):
    n = int(seconds * RATE)
    env = envelope(n, attack, decay)
    out, phase = [], 0.0
    for i in range(n):
        t = i / n
        f = f0 * (f1 / f0) ** t
        phase += 2 * math.pi * f / RATE
        out.append(math.sin(phase) * env[i])
    return out


def lowpass(samples, alpha):
    out, y = [], 0.0
    for s in samples:
        y += alpha * (s - y)
        out.append(y)
    return out


def mix(*tracks, offsets=None):
    offsets = offsets or [0] * len(tracks)
    n = max(len(t) + int(o * RATE) for t, o in zip(tracks, offsets))
    out = [0.0] * n
    for t, o in zip(tracks, offsets):
        start = int(o * RATE)
        for i, s in enumerate(t):
            out[start + i] += s
    return out


def bubble():
    """A bubble popping at the surface: a quick upward chirp."""
    return sweep(0.07, 700, 1400, 0.02)


def plop():
    """Food landing on the water."""
    rng = random.Random(3)
    body = sweep(0.16, 320, 110, 0.05)
    splash = lowpass([rng.uniform(-1, 1) * math.exp(-i / (0.012 * RATE)) for i in range(int(0.05 * RATE))], 0.25)
    return mix(body, [s * 0.4 for s in splash])


def nom():
    """A fish taking a pellet: two soft clicks."""
    rng = random.Random(5)
    click = lowpass([rng.uniform(-1, 1) * math.exp(-i / (0.004 * RATE)) for i in range(int(0.03 * RATE))], 0.15)
    return mix(click, [s * 0.7 for s in click], offsets=[0, 0.07])


def chime():
    """A fish being petted: two gentle notes."""
    return mix(sweep(0.45, 880, 880, 0.15, 0.01), [s * 0.7 for s in sweep(0.5, 1318, 1318, 0.18, 0.01)],
               offsets=[0, 0.12])


def hatch():
    """Eggs hatching: a little run of pops."""
    return mix(*[sweep(0.06, 900 + 150 * k, 1700 + 150 * k, 0.015) for k in range(4)],
               offsets=[0, 0.09, 0.15, 0.24])


def ambient():
    """A quiet, loopable water bed: soft rumble and the odd far-off bubble."""
    rng = random.Random(11)
    seconds = 12
    n = seconds * RATE
    noise = lowpass(lowpass([rng.uniform(-1, 1) for _ in range(n)], 0.02), 0.05)
    # Slow swell so it doesn't sound like static
    bed = [s * (0.75 + 0.25 * math.sin(2 * math.pi * i / n * 3)) for i, s in enumerate(noise)]
    out = bed[:]
    for _ in range(9):
        b = [s * 0.08 for s in sweep(0.08, rng.uniform(500, 900), rng.uniform(1100, 1600), 0.025)]
        start = rng.randint(0, n - len(b) - 1)
        for i, s in enumerate(b):
            out[start + i] += s
    # Crossfade the ends so the loop is seamless
    fade = RATE // 2
    for i in range(fade):
        k = i / fade
        out[i] = out[i] * k + out[n - fade + i] * (1 - k)
    return out[: n - fade]


OUT.mkdir(parents=True, exist_ok=True)
for name, make in {
    "bubble.wav": bubble,
    "plop.wav": plop,
    "nom.wav": nom,
    "chime.wav": chime,
    "hatch.wav": hatch,
    "ambient.wav": ambient,
}.items():
    write(name, make())
    print(f"wrote {name}")
