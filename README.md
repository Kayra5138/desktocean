# Desktocean

A lightweight, living aquarium wallpaper for KDE Plasma 6. Fish swim behind your
desktop icons and widgets; later you'll be able to draw your own fish and
decorations and drop them into the tank.

> Status: **Phase 0b.** Placeholder fish, swimming, feeding, auto-pause, and a
> Rust simulation core running inside plasmashell. Drawing and decorations are
> on the roadmap below.

## Features (so far)

- Runs as a native Plasma wallpaper plugin, so desktop icons and widgets stay on top and keep working
- Fish textures bend along the spine in a vertex shader, so a flat drawing swims convincingly
- Water, light rays, caustics, sand and bubbles in a single fragment shader
- Right-click the desktop → **Feed fish** (drops food above the cursor) / **Pause aquarium**
- A curious fish follows the cursor while it moves, and loses interest when it stops
- Simulation written in Rust; a panic inside it freezes the aquarium instead of crashing plasmashell
- Stops ticking and redrawing while a maximized or fullscreen window covers the screen
- Capped frame rate (default 30 FPS); zero redraws while paused

## Requirements

- KDE Plasma 6 (developed on 6.8, Wayland)
- Qt 6 development files and the `qsb` shader tool (`qt6-base`, `qt6-declarative`, `qt6-shadertools`)
- CMake, a C++20 compiler, and Rust (cargo)
- Python 3 (generates the placeholder fish)

## Install

```bash
./scripts/install.sh
```

This builds the `Desktocean.Core` QML plugin into `~/.local/lib/qt6/qml` and adds
that directory to `QML_IMPORT_PATH` via `~/.config/environment.d/50-desktocean.conf`
(and for the running session via `systemctl --user set-environment`).

Then right-click the desktop → *Desktop and Wallpaper* → *Wallpaper type* → **Desktocean**.

After changing code, run `./scripts/install.sh` again and restart plasmashell so it
reloads the QML (desktop and panels blink for a second):

```bash
./scripts/reload-plasma.sh
```

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

### Layout

| Path | What |
|---|---|
| `package/` | The KPackage installed into Plasma |
| `package/contents/ui/main.qml` | Plasma glue: config, context menu, pause logic |
| `package/contents/ui/Aquarium.qml` | Scene + simulation (no Plasma imports) |
| `package/contents/ui/Fish.qml` | One fish (shader-bent texture) |
| `sim/` | Rust simulation core (static library, C ABI in `sim/include/`) |
| `native/` | C++ QML plugin `Desktocean.Core`: wraps the core, exposes the cursor |
| `shaders/` | GLSL sources, compiled to `.qsb` into the package |
| `scripts/` | Install, build, reload and measure helpers |
| `dev/preview.qml` | Standalone preview window |

## Roadmap

1. ~~**Phase 0b** — load a Rust module from inside plasmashell~~
2. **Phase 1** — Rust simulation (boids, personalities), batched fish rendering
3. **Phase 2** — drawing editor and PNG import
4. **Phase 3** — behaviour: feeding, cursor curiosity, friendships
5. **Phase 4** — swaying plants, rocks, coral, themes
6. **Phase 5** — settings, day/night cycle, system integrations, companion widget

Inspired by [Drawquarium](https://store.steampowered.com/app/4045480/Drawquarium/).

## License

[GPL-3.0-or-later](LICENSE)
