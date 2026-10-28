#!/usr/bin/env python3
"""Generates the placeholder fish SVGs used by the spike (head faces right)."""
import pathlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "package/contents/images"

TEMPLATE = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 120" width="400" height="240">
  <g stroke="{out}" stroke-linejoin="round" stroke-linecap="round">
    <path d="M54 60 C 38 44, 22 26, 7 17 C 16 40, 16 80, 7 103 C 22 94, 38 76, 54 60 Z" fill="{fin}" stroke-width="3"/>
    <path d="M88 32 C 100 9, 130 5, 148 27 Z" fill="{fin}" stroke-width="3"/>
    <path d="M46 60 C 70 20, 152 16, 193 58 C 154 102, 72 102, 46 60 Z" fill="{body}" stroke-width="3.5"/>
    <path d="M72 72 C 100 92, 152 90, 181 68 C 152 83, 100 85, 72 72 Z" fill="{belly}" stroke="none" opacity="0.75"/>
    <path d="M110 27 C 101 50, 101 72, 110 93" stroke="{stripe}" stroke-width="8" fill="none"/>
    <path d="M140 28 C 133 50, 133 72, 140 89" stroke="{stripe}" stroke-width="7" fill="none"/>
    <path d="M120 70 C 113 83, 101 89, 92 87 C 99 78, 107 72, 120 70 Z" fill="{fin}" stroke-width="2.5"/>
    <circle cx="168" cy="51" r="7.5" fill="#ffffff" stroke-width="2"/>
    <circle cx="170.5" cy="51" r="3.8" fill="#16182a" stroke="none"/>
    <circle cx="172" cy="49" r="1.3" fill="#ffffff" stroke="none"/>
    <path d="M187 65 q -5 3 -10 1" stroke-width="2.5" fill="none"/>
  </g>
</svg>
"""

FISH = {
    "orange": dict(body="#f28c28", fin="#f7b267", belly="#ffd29a", stripe="#fff4e0", out="#6b3410"),
    "blue":   dict(body="#3d8bd9", fin="#7fc3f0", belly="#bfe3ff", stripe="#1f4f8a", out="#173a63"),
    "pink":   dict(body="#d36bb0", fin="#f2a7d8", belly="#ffd6f0", stripe="#8a3f9e", out="#5a1f4f"),
    "lime":   dict(body="#e9d23a", fin="#9fd356", belly="#fff3a8", stripe="#3e7d2a", out="#5a5212"),
}

OUT.mkdir(parents=True, exist_ok=True)
for name, colors in FISH.items():
    (OUT / f"fish-{name}.svg").write_text(TEMPLATE.format(**colors))
    print(f"wrote fish-{name}.svg")
