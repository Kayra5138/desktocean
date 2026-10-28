# Desktocean

A lightweight, living aquarium wallpaper for KDE Plasma 6. Fish swim behind your
desktop icons and widgets; later you'll be able to draw your own fish and
decorations and drop them into the tank.

> Status: **Phase 4.** Draw your own fish, plants and decorations (or import a
> picture) and arrange your tank. Fish get hungry, make friends and rivals,
> greet newcomers and rest at night.

## Features (so far)

- **Drawing editor** (`desktocean-editor`, or right-click the desktop → **Draw a fish…**):
  brush, eraser, bucket fill, colour palette, undo/redo, outline templates to colour in,
  picture import with background removal, flip; **My aquarium** page to set how many
  copies of each fish swim, or remove them
- New fish drop into the tank through the surface as soon as you save them
- **Swim styles** per fish (classic, eel, stiff tail, bouncy, glider) and a **wiggle brush**
  to paint which parts move more (fins, tail) or stay stiff, with a live swimming preview
  in the editor; no skeleton needed, it is the same cheap spine bend with per-vertex weights

- Runs as a native Plasma wallpaper plugin, so desktop icons and widgets stay on top and keep working
- Fish sprites bend along the spine, so a flat drawing swims convincingly; all fish and food are drawn in a single draw call
- Every fish has a **character** that shapes how it lives: explorer (roams far), homebody (keeps to
  its own patch), bottom dweller, skimmer (upper water), lazy (rests a lot), playful (sudden dashes)
  or shy (keeps its distance, swims away from the cursor). It stays the same across restarts and is
  shown on the editor's fish cards
- Fish spread out over the whole tank: they pick the least crowded of a few spots to swim to
- Fish of the same kind (copies of one drawing) sometimes form a school for a minute or so
- Food is noticed one fish at a time, depending on how alert each one is and how far away it is
- A curious fish only notices the cursor once it has lingered near it for a second, then follows it
  for a while
- **Hunger**: fish get hungry over time and look for food near the surface; full fish ignore food and rest more
- **Friends and rivals**: fish that spend time together become friends and swim side by side; snatching
  each other's food makes rivals, who chase each other now and then. Shown on the editor's fish cards
- **Day and night** follow the clock: at night the water darkens, plankton glows and fish rest near the bottom
- **Newcomers**: curious fish come over to look at a fish that just arrived
- **Babies**: close, well-fed friends now and then have a baby. It is born small between its
  parents, shaped like one and coloured toward the other, and grows up over a couple of hours.
  Babies are saved like any fish, so you can rename, redraw or remove them. A setting caps the
  number of fish
- **Plants and decorations**: draw them too; plants sway in the current, decorations stand still.
  Arrange them on the editor's **Tank layout** page (drag, resize, in front of or behind the fish,
  flip, copy). Starter seaweed, rocks and a coral are included
- **Themes** for water and sand: Tropical, Lagoon, Deep sea, Kelp forest, Volcanic
- **Desktocean widget** for the panel or desktop: feed, pause, open the editor, and see how
  hungry the fish are and how many friendships they made
- **Your computer in the water**: when a notification arrives a fish swims up and blows bubbles
  (not in Do Not Disturb); when the CPU is busy the current picks up, plants sway harder and more
  bubbles rise. Fish also let out a few bubbles now and then. Both can be turned off
- Water, light rays, caustics, sand and bubbles in a single fragment shader
- Right-click the desktop → **Desktocean** → Feed fish (drops food above the cursor), Clear food,
  Draw a fish…, My fish…, Tank layout…, Pause aquarium
- Fish eat only with their **mouth** (marked in the editor with the Mouth tool, or guessed from the
  front of the drawing) and stop for a moment to swallow; brushing past food doesn't count
- Unlimited food; **Clear food** removes it
- Simulation written in Rust; a panic inside it freezes the aquarium instead of crashing plasmashell
- Stops ticking and redrawing while a maximized or fullscreen window covers the screen
- Capped frame rate (default 30 FPS); zero redraws while paused

## Requirements

- KDE Plasma 6 (developed on 6.8, Wayland) with Kirigami
- Qt 6 development files and the `qsb` shader tool (`qt6-base`, `qt6-declarative`, `qt6-shadertools`)
- CMake, a C++20 compiler, and Rust (cargo)
- Python 3 (generates the placeholder fish)

## Install

```bash
./scripts/install.sh
```

This installs `desktocean-editor` into `~/.local/bin` (plus an app menu entry), builds
the `Desktocean.Core` QML plugin into `~/.local/lib/qt6/qml` and adds
that directory to `QML_IMPORT_PATH` via `~/.config/environment.d/50-desktocean.conf`
(and for the running session via `systemctl --user set-environment`).

Then right-click the desktop → *Desktop and Wallpaper* → *Wallpaper type* → **Desktocean**.

After changing code, run `./scripts/install.sh` again and restart plasmashell so it
reloads the QML (desktop and panels blink for a second):

```bash
./scripts/reload-plasma.sh
```

## Your fish

Fish live in `~/.local/share/desktocean/fish/`: one PNG per fish (head facing right),
an optional `.wiggle.png` motion mask, and a `fish.json` manifest. Friendships are kept
in `~/.local/share/desktocean/state.json`. On first run the tank is seeded with four starter fish;
remove or keep them from the editor's **My aquarium** page.

## Development

Run the aquarium in a plain window, without touching plasmashell:

```bash
/usr/lib/qt6/bin/qml -I build/qml dev/preview.qml
```

Click to feed, <kbd>Space</kbd> to pause, <kbd>S</kbd> to save `dev/preview.png`.
Run `./scripts/build-shaders.sh` after editing anything in `shaders/`, and
`cmake --build build` after editing `sim/` or `native/`. Rust tests: `cargo test` in `sim/`.

`./scripts/measure.sh [seconds]` samples plasmashell CPU and memory; compare a run
with the aquarium paused against one with it running.

`DESKTOCEAN_PROFILE=1` makes the tank log its average simulation and geometry
time every 5 s; `dev/preview.qml -- --fullscreen --fps 30` measures at screen size.
Measured on a 2560x1440 screen with 30 fish (Ryzen-class CPU, RTX 5070 Ti):

| FPS | CPU (one core) | GPU |
|---|---|---|
| 20 | ~3.3% | <1% |
| 30 (default) | ~4.1% | <1% |
| 60 | ~10.5% | ~0.5% |
| screen refresh (165 Hz) | ~18% | ~3% |

Simulation and geometry take ~0.3 ms per frame together; the rest is Qt Quick's
fixed per-frame cost, so the frame rate setting is the knob that matters.
"Match the screen's refresh rate" (off by default) ticks once per refresh,
in step with the display, instead of on a timer. The
aquarium adds about 60 MB of memory, mostly the sprite atlas.

### Layout

| Path | What |
|---|---|
| `package/` | The KPackage installed into Plasma |
| `package/contents/ui/main.qml` | Plasma glue: config, context menu, pause logic |
| `package/contents/ui/Aquarium.qml` | Scene: water shader, FishTank, diagnostics (no Plasma imports) |
| `sim/` | Rust simulation core (static library, C ABI in `sim/include/`) |
| `native/` | C++ QML plugin `Desktocean.Core`: `FishTank` ticks the core, reads the cursor, batch-renders sprites |
| `native/editor/` | `desktocean-editor`: drawing canvas, fish library page (Kirigami) |
| `native/src/fishlibrary.*` | The fish library on disk, shared by plugin and editor |
| `native/src/fishmesh.*`, `spritenode.*` | Swim styles, mesh bending and batched sprite rendering, shared by tank and editor preview |
| `shaders/` | Water shader source, compiled to `.qsb` into the package |
| `scripts/` | Install, build, reload and measure helpers |
| `applet/` | The Desktocean control widget (Plasma applet) |
| `dev/preview.qml` | Standalone preview window |

## Roadmap

1. ~~**Phase 0b** — load a Rust module from inside plasmashell~~
2. ~~**Phase 1** — Rust simulation (boids, personalities), batched fish rendering~~
3. ~~**Phase 2** — drawing editor and PNG import~~
4. ~~**Phase 3** — behaviour: hunger, friendships and rivalries, day/night, newcomers~~
5. ~~**Phase 4** — swaying plants, rocks, coral, themes~~
6. ~~**Phase 5** — companion widget, scenery templates, system integrations~~

Inspired by [Drawquarium](https://store.steampowered.com/app/4045480/Drawquarium/).

## License

[GPL-3.0-or-later](LICENSE)
