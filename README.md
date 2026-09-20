# xmbwave

A standalone C++17 live-wallpaper renderer that recreates the PlayStation 3 XMB
background wave. It draws straight onto override-redirect desktop windows and
targets X11.

Note: not tested on Wayland :)

```
   CPU (AVX2/FMA)                    GPU (GLX)                     X11
   displacement grid   --->   texture-less vertex mesh   --->   desktop windows
   256 x 96 (dy, dz)          fresnel glow + gradient          one per monitor
                              additive sparkle particles
```

The wave displacement is computed on the CPU into a coarse vertex grid, uploaded
as vertex attributes, and shaded on the GPU. The CPU grid is small enough to be
cheap and coarse enough to be skippable: when the wallpaper is not visible, the
whole loop idles.

So, its primarly optimized for CPU which supports AVX2 instrustion set. 
In case of AVX2 not being available - trust the compiler to optimize the code based on the supported instruction set (-march=native).

## What it does

- Recreates the XMB wave with the RetroArch `pipeline_ribbon` displacement math
  and fresnel shading, the same pair OpenXMB uses.
- Adds the additive sparkle particle layer (point sprites).
- Runs as a live wallpaper: creates one override-redirect, click-through
  desktop window per monitor. No helper program needed.
- Pauses the wave computation and the GL draw whenever the wallpaper is not
  visible, and is multi-monitor aware:
  - a normal focused window on monitor A animates only monitor A;
  - a fullscreen window pauses the covered monitor (and everything else, unless
    `allMonitors = 1`);
  - when the desktop itself is focused, every monitor animates.
- Configured by a config file and/or `--set` command-line overrides.

## Build

```sh
git clone --recursive <this repo>   # spdlog is a submodule
make -j$(nproc)                     # -> build/xmbwave
make check                          # wave kernel + visibility policy checks
sudo make install
```

If you cloned without `--recursive`, `make` fetches the submodule for you.

Requires `g++` (C++17), `libx11`, `libxext`, `libxrandr`, `libgl` and their
headers. Fedora: `gcc-c++ libX11-devel libXext-devel libXrandr-devel
mesa-libGL-devel`. Debian/Ubuntu: `g++ libx11-dev libxext-dev libxrandr-dev
libgl-dev`.

## Configuration

A config file, optionally overridden per run. The file is read from
`--config <path>`, or from `$XDG_CONFIG_HOME/xmbwave/xmbwave.conf`
(`~/.config/xmbwave/xmbwave.conf`).

```sh
xmbwave --set fps=30 --set grid=320x120 --set pauseUnfocused=0
xmbwave --config ~/alt.conf --set speed=0.4
```

Plain `key = value` lines in the file, `#` starts a comment. The file and
`--set` accept the same keys and the same value ranges. See
[`config/xmbwave.conf`](config/xmbwave.conf) for the annotated list.

| Key | Default | Meaning |
|-----|---------|---------|
| `fps` | 60 | target frame rate |
| `vsync` | 0 | also block on vblank |
| `msaa` | 4 | multisample count; the ribbon's strand edges alias badly without it |
| `gridW`, `gridH` | 256, 96 | CPU displacement grid |
| `speed` | 1.0 | global time multiplier |
| `amplitude` | 1.0 | overall displacement gain |
| `tension` | 0.12 | spline/ribbon tension |
| `detail` | 4.5 | value-noise frequency (the kernel stand-in) |
| `ribbonScale`, `softClip` | 0.5, 0.22 | ribbon height and soft clip |
| `zDetailScale` | 0.08 | depth offset from the scrolled second sample |
| `brightness` | 1.1 | crest glow multiplier |
| `opacity` | 1.0 | ribbon alpha |
| `fresnelPower`, `fresnelScale` | 4.0, 0.5 | crease falloff and gain |
| `waveBody` | 0.0 | flat-area luminance of the curtain (0 = crease only) |
| `colorTop`, `colorBot` | dark blue / cyan | background gradient, top and bottom (0..1 or 0..255) |
| `waveColor` | light blue | tint of the wave strands |
| `gradientAngle` | 15.1 | background gradient angle in degrees from vertical |
| `particles` | 220 | sparkle count, 0 disables |
| `particleOpacity` | 0.55 | sparkle alpha |
| `particleSize`, `particleSizeVar` | 1.5, 4.0 | point size and size spread |
| `particleSpeed` | 1.0 | sparkle speed relative to the wave |
| `pauseUnfocused` | 1 | focus-aware pausing |
| `allMonitors` | 0 | animate every visible monitor |
| `focusPollMs` | 250 | focus poll interval |
| `logLevel` | 2 | 0=error 1=warn 2=info 3=debug |

Other options: `--selftest`, `--version`, `--help`.

## Self-test

```sh
make check     # or: build/xmbwave --selftest
```

Computes the field with the scalar reference and the AVX2 kernel on a grid with
odd dimensions (to exercise the tail), at several times including large ones,
and checks that they agree to `1e-4` and stay finite and bounded. The large-time
cases cover the reduced-argument path.

It also table-tests the visibility policy (`shouldRenderSurface`), since that
branch decides the whole pause/multi-monitor behaviour and cannot be exercised
without a window manager.

## AI

Used AI to reverse engineer the PS3 firmware and write shaders for the wave and the particles (and write this README a little bit).
