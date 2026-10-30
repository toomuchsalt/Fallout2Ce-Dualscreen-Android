# Armada OS / AYN Thor

Native ARM64 Linux port of the touch-first Android fork. Gameplay, scripts,
mobile screens, save format, and the bottom-screen dashboard remain in C++.
The Android app and JNI are not required. Baseline source:
`098a31ce092052d113911159a74e7cfa7bf3165e`. Rebased onto upstream main
`ff7d72a3` on 2026-10-30.

## Build and package

Native development build (SDL2 and zlib development packages, CMake, C++17):

```sh
cmake --preset armada
cmake --build --preset armada -j8
ctest --test-dir out/build/armada --output-on-failure
cmake --install out/build/armada --prefix "$PWD/out/stage/host"
```

On x86-64, build an ARM64 package without installing a compiler on the Thor:

```sh
podman build -t fallout2-armada-cross -f os/linux/Containerfile.cross .
podman run --rm --userns=keep-id -v "$PWD:/src:Z" fallout2-armada-cross \
  cmake -S . -B out/build/armada-aarch64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain/armada-aarch64.cmake \
  -DFALLOUT_ARMADA=ON -DFALLOUT_VENDORED=OFF \
  -DFALLOUT_DIAGNOSTICS=OFF -DCMAKE_BUILD_TYPE=Release
podman run --rm --userns=keep-id -v "$PWD:/src:Z" fallout2-armada-cross \
  cmake --build out/build/armada-aarch64 --target fallout2-ce fallout2-companion fallout2-setup -j8
podman run --rm --userns=keep-id -v "$PWD:/src:Z" fallout2-armada-cross \
  cmake --install out/build/armada-aarch64 --prefix /src/out/stage/armada-aarch64/Fallout2-Armada --strip
tar -C out/stage/armada-aarch64 -czf out/Fallout2-Dualscreen-Armada-aarch64.tar.gz Fallout2-Armada
```

The cross container uses Debian 13's older glibc baseline; binaries use
Armada's native SDL2 ABI (SDL2-compat on the tested Fedora 44 image), zlib,
and C++ runtime. No emulation is used to run the game. Native ARM builds can
use the same `armada` preset. Source archives work without Git history;
version headers are generated in the build directory.

## GitHub Actions builds

The Armada job builds natively on GitHub's `ubuntu-24.04-arm` runner, using
our `armada` CMake preset with Release optimization. SDL2/zlib development
packages are installed only on the disposable CI runner. No Docker image,
cross-toolchain, or emulation is needed in CI; the cross-container instructions
above remain available for local x86-64 development. The archive layout and
graphical installation on immutable Armada are unchanged.

The Build workflow produces separate `Fallout2-Dualscreen-apk` and
`Fallout2-Dualscreen-armada-aarch64` artifacts for pull requests, manual runs,
and main/tag pushes. The Armada artifact contains
`Fallout2-Dualscreen-Armada-aarch64.tar.gz` and its SHA-256 checksum, preserving
executable permissions inside the archive. Main and version-tag pushes publish
both platforms through a single release job after both builds succeed.

## Install and launch

Use the [terminal-free installation guide](armada-installation.md) for end users.
The archive has one `Fallout2-Armada` folder, a native **Install Fallout 2**
bootstrap, and an offline **START HERE.html** guide. Graphical setup uses
Armada's existing Python 3 and KDialog. No system packages or root access are
needed. It installs to `~/Applications/Fallout2-Armada`, writes an absolute
application-menu shortcut, and remembers the game folder in
`~/.config/fallout2-armada/launcher.json`. Game data and saves remain separate.
Updates and removal refuse to run while the managed game is running. Removal
only deletes a marked application tree and keeps the chosen game folder.

For developer use, `bin/fallout2-armada --game-dir /path/to/game` still works.
`--gui` enables graphical errors and first-run folder selection;
`--gui --choose-game` changes the remembered folder without launching.
`--mode game|desktop` overrides session detection; `--single-screen` keeps UI
on the top screen. Steam must launch natively with compatibility override off.
The launcher uses Armada's existing bottom Gamescope service and
`armada-run-bottom`; it does not configure fans or power profiles.
In KDE, connector names DSI-2/DSI-1 take priority over logical and physical
geometry fallbacks, including fractional display scaling.

## Controls and troubleshooting

Both touchscreens use the fork's native touch UI. In Steam Game Mode's X11
session, the main game also converts mouse-style touchscreen events into
finger input: Steam can select left-click mode for Non-Steam games. Native
multitouch remains available when Steam's touchscreen mode is passthrough.
The companion already forwards both native touches and mouse-style touches. Controller support follows upstream, including configurable bindings,
UI focus navigation, the map cursor, and the quick-use ring. See the
[upstream controller guide](gamepad-stage3.md) for controls and settings.

Text fields display an in-engine touch keyboard with ASCII letters, numbers,
case toggle, punctuation, space, delete, and Done. Physical keyboard text
(including supported Unicode) and editing keys work with either window
focused. Available characters are still limited by Fallout's game encoding.

Environment overrides:

| Variable | Purpose |
| --- | --- |
| `FALLOUT_TOP_OUTPUT` | SDL display name or explicit numeric index; desktop default `DSI-2` |
| `FALLOUT_BOTTOM_OUTPUT` | Desktop companion output; default `DSI-1` |
| `FALLOUT_TOP_DENSITY` | Physical pixels per UI dp; default 2.0 |
| `FALLOUT_BOTTOM_DENSITY` | Companion UI density; default 2.6 |
| `FALLOUT_COMPANION=0` | Disable helper when starting the engine directly |
| `FALLOUT_COMPANION_WINDOWED=1` | Companion development window |

Launcher diagnostics rotate at 1 MiB in
`~/.local/state/fallout2-armada/launch.log` and `launch.log.1` (or under
`XDG_STATE_HOME`). In-game debug logging follows the existing configuration.
If a helper disappears, its touches are canceled and the UI falls back to
the top; the helper retries after two seconds. A hidden window cancels input
and falls back without repeatedly destroying the compositor connection.
A temporarily sleeping compositor may defer video initialization until wake.

## Architecture and verification

The game renders both UI surfaces on its original renderer and game thread.
The Linux backend implements `bottom_screen.h` and spawns a frame-only SDL
helper. Two shared-memory frame slots use lock-free ownership states; the
producer drops/replaces unread frames rather than waiting. A private inherited
Unix `SOCK_SEQPACKET` connection carries dimensions/density, visibility,
focus, text, touches, and heartbeat messages. Pixels are RGBA, tightly packed
in shared memory; padded source pitches are supported. The transport supports
outputs up to 2560×2560 and bounds packet processing per input pass.

Touch IDs include device identity. Forwarded touches have explicit ownership,
clock-based stale-event rejection, and cancellation on resize, disconnect,
focus loss, and hidden windows. Focus is evaluated across both processes with
a short transfer grace period. The helper opens no game controller and never
reads or changes game state. Presentation sleeps when no frame changes.

Automated checks cover save retention/transactions, audio tempo, launcher file
preservation and argument handling, process-shared frames/backpressure, and
real child-process input/frame transport including stale touches and EOF.

For a self-contained gameplay capture, use a disposable game copy, skip intro
movies (`[ui] skip_opening_movies=1`), and launch:

```sh
bin/fallout2-armada --game-dir /path/to/test-copy -- \
  --dev-new-game --dev-autotest-scenario=armada --dev-autotest=/path/to/existing-parent/captures
```

This opens the companion screens, types `qW` through the touch keyboard,
saves, and reloads. It writes a real save into the test copy. Captures include
both surfaces when the companion is active. The launcher enables bottom frame
capture only for autotests. Confirm `autotest finished`, no `FAIL`/`MISMATCH`
messages, and a saved description of `qW`.

`companion-preview` is an optional build target for display-only smoke tests;
it needs no game data. Launch with the same companion launcher/binary variables
as the game. `FALLOUT_COMPANION_CAPTURE=/path/frame.bmp` saves its first
presented frame for diagnostics.

Remaining device acceptance checks: physical touch gestures on both screens,
controller hotplug, Steam overlay focus, lid/suspend/resume, alternate dashboard
layouts, external-monitor attachment, and dual-window placement in KDE. These
must be distinguished from automated software event injection.

## Validation record (2026-10-29)

Tested on an AYN Thor / SM8550 running Armada `20261028.f0a50c0`:

- The initial four native C++ tests passed on ARM64, along with the host's
  Python launcher suite. The separate quick-save ordering fix was subsequently
  reverted as requested: `save_catalog_test` again reports the upstream range-
  resizing failure at line 351. The audio and companion tests remain passing.
- Controller stall/disconnect resets and extra UI drag/capture cleanup are
  compiled only for Armada; Android retains its original behavior.
- Bottom-compositor smoke test presented 86 frames at 1240×1080, including
  a run with the bottom Plasma desktop stopped; its prior state was restored.
- The `armada` gameplay scenario completed without failure markers, capturing
  world, inventory, character, Pip-Boy, automap, text entry, save, and reload
  on both surfaces. `SAVE.DAT` contains the keyboard-entered `qW` description.
- The same scenario completed locally with the offscreen SDL driver in both
  single-screen and dual-process configurations.
- No fan settings, power profiles, or system services were replaced. Test
  processes were stopped when validation finished.

Hardware display initialization was observed to block while the device was
asleep, then succeed after wake. Helper startup has a timeout/retry path and
parent-death cleanup. A complete lid/suspend/resume interaction test is still
required. KDE display placement was subsequently validated at 150% scaling, including
user confirmation that the companion test pattern appeared on the physical
bottom screen. Connector names take precedence over geometry; centered fullscreen
placement explicitly targets the chosen output.
