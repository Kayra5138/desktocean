#!/usr/bin/env python3
"""Generates the starter plant and decoration SVGs (bottom edge sits on the sand)."""
import math
import pathlib
import random

OUT = pathlib.Path(__file__).resolve().parent.parent / "package/contents/images"


def svg(w, h, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w * 2}" height="{h * 2}">\n'
            f'{body}\n</svg>\n')


def blade(rng, x0, base, height, sway, width, fill, stroke):
    """One seaweed blade: a tapered wavy ribbon from (x0, base) upward."""
    left, right = [], []
    steps = 12
    for i in range(steps + 1):
        t = i / steps
        y = base - t * height
        cx = x0 + math.sin(t * math.pi * 1.6 + sway) * width * 1.4 * t
        half = width * (1 - t) * 0.5 + 1.5
        left.append((cx - half, y))
        right.append((cx + half, y))
    pts = left + right[::-1]
    d = "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in pts) + " Z"
    return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="2.5" stroke-linejoin="round"/>'


def seaweed_tall():
    rng = random.Random(4)
    parts = []
    for i, (x, hgt, col) in enumerate([(45, 360, "#3f9a4a"), (70, 390, "#4fb35a"), (95, 330, "#2f7d3a"), (60, 300, "#5cc06a")]):
        parts.append(blade(rng, x, 398, hgt, i * 1.3, 22, col, "#1f5428"))
    return svg(140, 400, "\n".join(parts))


def seaweed_bushy():
    rng = random.Random(9)
    parts = []
    for i in range(9):
        x = 40 + i * 22 + rng.uniform(-6, 6)
        hgt = rng.uniform(110, 200)
        col = rng.choice(["#2e8b57", "#3aa36a", "#24704a", "#47b37a"])
        parts.append(blade(rng, x, 218, hgt, rng.uniform(0, 6), 16, col, "#174a30"))
    return svg(260, 220, "\n".join(parts))


def rock():
    body = """  <path d="M 18 150 C 8 110, 40 60, 95 48 C 140 30, 205 38, 245 70 C 285 100, 296 138, 284 152 Z"
        fill="#8a8f98" stroke="#4c5058" stroke-width="4" stroke-linejoin="round"/>
  <path d="M 60 80 C 95 58, 150 52, 190 62 C 150 66, 100 74, 70 92 Z" fill="#b4b9c2" opacity="0.8"/>
  <path d="M 150 130 C 190 118, 235 120, 262 140 C 230 136, 190 138, 150 146 Z" fill="#6c717a" opacity="0.8"/>
  <circle cx="110" cy="118" r="7" fill="#6c717a"/>
  <circle cx="205" cy="95" r="5" fill="#6c717a"/>
  <circle cx="80" cy="128" r="4" fill="#9ea3ac"/>"""
    return svg(300, 160, body)


def coral():
    rng = random.Random(7)
    parts = []

    def branch(x, y, angle, length, width, depth):
        if depth == 0 or length < 12:
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{width * 0.9 + 3:.1f}" fill="#ff8fab" stroke="#b03a62" stroke-width="2.5"/>')
            return
        x2 = x + math.cos(angle) * length
        y2 = y - math.sin(angle) * length
        parts.append(f'<line x1="{x:.1f}" y1="{y:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#b03a62" '
                     f'stroke-width="{width + 5:.1f}" stroke-linecap="round"/>')
        parts.append(f'<line x1="{x:.1f}" y1="{y:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#f06292" '
                     f'stroke-width="{width:.1f}" stroke-linecap="round"/>')
        for da in (-0.45, 0.4):
            branch(x2, y2, angle + da + rng.uniform(-0.15, 0.15), length * rng.uniform(0.62, 0.78), width * 0.72, depth - 1)

    branch(120, 258, math.pi / 2, 80, 22, 4)
    return svg(240, 260, "\n".join(parts))


ITEMS = {
    "decor-seaweed-tall.svg": seaweed_tall,
    "decor-seaweed-bushy.svg": seaweed_bushy,
    "decor-rock.svg": rock,
    "decor-coral.svg": coral,
}

OUT.mkdir(parents=True, exist_ok=True)
for name, make in ITEMS.items():
    (OUT / name).write_text(make())
    print(f"wrote {name}")
