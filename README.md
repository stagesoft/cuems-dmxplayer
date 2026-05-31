<!--
***
SPDX-FileCopyrightText: 2020-2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
***
-->

# cuems-dmxplayer

**Current release: v0.0** — see [CHANGELOG.md](./CHANGELOG.md).

**A Linux DMX lighting player that streams MTC-synchronised DMX cues to OLA, driven over OSC.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-FCC624.svg)](https://www.kernel.org/)

* **Source / issues:** [stagesoft/cuems-dmxplayer](https://github.com/stagesoft/cuems-dmxplayer) on GitHub
* **Contributing:** see [CONTRIBUTORS.md](./CONTRIBUTORS.md)
* **Release history:** see [CHANGELOG.md](./CHANGELOG.md)

`cuems-dmxplayer` is the DMX lighting playback daemon for the **CUEMS** (Cue Management
System) platform. It receives lighting scenes and control commands over **OSC** (Open Sound
Control), interpolates per-channel DMX fades, and synchronises playback to **MIDI Time Code
(MTC)**. DMX output is emitted through the **OLA** (Open Lighting Architecture) daemon, so any
DMX transport OLA supports — USB-DMX dongles, Art-Net, sACN — is available without changes to
the player.

It is composed of the player executable plus three Git submodules that are reused across the
CUEMS daemon family:

  * **`oscreceiver`** — UDP OSC listener; the base class `DmxPlayer` derives from.
  * **`mtcreceiver`** — RtMidi-based MIDI Time Code decoder and play-head estimator.
  * **`cuemslogger`** — syslog-backed singleton logger shared by all CUEMS components.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
  - [Top-level module (this repository)](#top-level-module-this-repository)
  - [Submodule: `oscreceiver`](#submodule-oscreceiver)
  - [Submodule: `mtcreceiver`](#submodule-mtcreceiver)
  - [Submodule: `cuemslogger`](#submodule-cuemslogger)
  - [Internal data structures (in `DmxPlayer`)](#internal-data-structures-in-dmxplayer)
  - [Threading model](#threading-model)
- [Core Concepts](#core-concepts)
- [Design Goals](#design-goals)
- [API documentation](#api-documentation)
  - [OSC API](#osc-api)
    - [Control commands](#control-commands)
    - [Bundle-only messages](#bundle-only-messages)
  - [Command-line interface](#command-line-interface)
  - [C++ public API](#c-public-api)
  - [Exit codes](#exit-codes)
- [Installation](#installation)
  - [Prerequisites](#prerequisites)
  - [Build from source](#build-from-source)
  - [Debian package](#debian-package)
- [Usage](#usage)
- [Development](#development)
- [Contributors](#contributors)
- [Release notes](#release-notes)
- [Future developments](#future-developments)
  - [1. Automated test suite (`ctest`)](#1-automated-test-suite-ctest)
  - [2. Tests + coverage workflow (`.github/workflows/tests.yml`)](#2-tests--coverage-workflow-githubworkflowstestsyml)
  - [3. Documentation site workflow (`.github/workflows/gh-pages.yml`)](#3-documentation-site-workflow-githubworkflowsgh-pagesyml)
  - [4. Debian packaging & release workflow](#4-debian-packaging--release-workflow)
  - [Target badge set](#target-badge-set)
- [Copyright notice](#copyright-notice)
- [License](#license)

---

[↑ Back to Table of Contents](#table-of-contents)

## Overview

`cuems-dmxplayer` is a single long-running process. Lighting data and transport control enter
over OSC; a timecode source feeds MTC over MIDI; DMX frames leave through OLA. Three independent
threads cooperate around shared, mutex-protected state:

```
        OSC bundles / commands            MIDI Time Code (MTC)
        (UDP, from cuems-engine)          (RtMidi: ALSA / rtpmidid)
                  │                                  │
                  ▼                                  ▼
        ┌────────────────────┐              ┌───────────────────┐
        │   OscReceiver      │              │   MtcReceiver     │
        │  (listener thread) │              │ (RtMidi callback  │
        └─────────┬──────────┘              │     thread)       │
                  │ ProcessBundle/Message   └─────────┬─────────┘
                  ▼                                   │ estimatedCurrentHead()
        ┌─────────────────────────────────────────────────────────────┐
        │                       DmxPlayer                             │
        │                                                             │
        │  m_scenes  ──►  processScenes()  ──►  m_activeUniverses     │
        │  (queued)       (fetch + build)        (per-channel fades)  │
        │                                             │               │
        │                               updateActiveUniverses()       │
        └─────────────────────────────────────────────┬───────────────┘
                                                      │ SendDMX()
                                  (OLA SelectServer thread, 10 ms / 200 ms timer)
                                                      ▼
                                          ┌────────────────────┐
                                          │   olad (OLA)       │
                                          │  USB-DMX / Art-Net │
                                          │  / sACN output     │
                                          └────────────────────┘
```

What each layer does:

* **Ingest** — `OscReceiver` parses incoming UDP packets and dispatches OSC messages and
  bundles to `DmxPlayer`. A top-level bundle is assembled into one *scene transition*.
* **Timecode** — `MtcReceiver` decodes MTC quarter-frames and full frames on the RtMidi thread
  and exposes a filtered, extrapolated play-head in milliseconds.
* **Scheduling** — `DmxPlayer` queues scenes ordered by their MTC start time, fetches each
  universe's current DMX state from OLA, and converts the target values into per-channel linear
  fade transitions.
* **Output** — On every OLA timer tick the player advances the play-head, interpolates each
  active channel, and writes the resulting `DmxBuffer` to OLA.

---

[↑ Back to Table of Contents](#table-of-contents)

## Architecture

The runtime path is built around the central `DmxPlayer` class; the legacy XML cue classes
(`DmxCue_v0` / `DmxCue_v1`) are a separate, older loading path retained for reference.

### Top-level module (this repository)

* **`DmxPlayer`** (`dmxplayer.h` / `dmxplayer.cpp`) — the central orchestrator. Inherits from
  `OscReceiver` to receive OSC, owns an `MtcReceiver` for timecode, and drives an OLA
  `OlaClientWrapper` + `SelectServer`. Responsible for scene queuing, fade interpolation, the
  adaptive output timer, and automatic OLA reconnection.
* **`CommandLineParser`** (`commandlineparser.h` / `commandlineparser.cpp`) — minimal argv
  tokeniser exposing `optionExists()` and `getParam()` lookups for the CLI flags.
* **`main`** (`main.h` / `main.cpp`) — process entry point. Parses the command line, installs
  the `SIGTERM` / `SIGINT` / `SIGUSR1` handlers, constructs the singleton logger and the
  `DmxPlayer`, and calls `run()`.
* **`CuemsConstants`** (`cuems_constants.h`) — compile-time constants: DMX/universe/channel
  bounds, port range, timer intervals, look-ahead and reconnection delays.
* **`cuems_errors.h`** — process exit codes shared across CUEMS daemons (see
  [Exit codes](#exit-codes)).
* **`DmxCue_v0`** (`dmxcue_v0.h` / `dmxcue_v0.cpp`) and **`DmxCue_v1`** (`dmxcue_v1.h` /
  `dmxcue_v1.cpp`) — *legacy* XML cue parsers built on Xerces-C. They model
  scene → universe → channel hierarchies loaded from `dmx.xml` files. The current runtime path
  uses OSC bundles instead; these classes are not on the OSC playback path.

### Submodule: `oscreceiver`

* **`OscReceiver`** — wraps oscpack's `OscPacketListener` and a `UdpListeningReceiveSocket`.
  Spawns a dedicated listener thread (`threadedRun()`), holds the bound `oscPort` and the OSC
  address prefix (`oscAddress`), and exposes overridable `ProcessMessage()` /
  `ProcessBundle()` hooks. `DmxPlayer` is constructed with an **empty address prefix**, so OSC
  addresses are matched directly (e.g. `/quit`, `/frame`).

### Submodule: `mtcreceiver`

* **`MtcReceiver`** — derives from `RtMidiIn`. Decodes MTC quarter-frame and full-frame SysEx
  messages on the RtMidi callback thread and maintains a filtered, extrapolated play-head.
  Key public surface used by the player:
  * `isTimecodeActive()` — whether timecode is currently advancing (network-tolerant timeout).
  * `estimatedCurrentHead()` — current MTC head in milliseconds, extrapolated between frames.
  * `setNetworkMode(bool)` — relaxes timeouts for MTC over the network (e.g. `rtpmidid`).
  * Static atomics `isTimecodeRunning`, `mtcHead`, `curFrameRate`, `wasLastUpdateFullFrame`.
  * `setTickCallback()` — optional lock-free per-quarter-frame callback (unused by the player).
* **`MtcFrame`** — value type for a decoded `hh:mm:ss:ff` frame plus rate, with conversions to
  and from seconds/milliseconds.

### Submodule: `cuemslogger`

* **`CuemsLogger`** — singleton syslog logger with severity helpers (`logError`, `logWarning`,
  `logInfo`, `logOK`, …). Retrieved anywhere via `CuemsLogger::getLogger()`. The per-process
  "slug" is set from the `--uuid` (or port) so log lines are attributable to a specific player.

### Internal data structures (in `DmxPlayer`)

| Type | Role |
|---|---|
| `SceneTransitionInfo` | One pending scene: target values per universe, MTC start time, fade duration. |
| `ChannelTransition` | Per-channel linear interpolation state: `mtc0`→`mtc1`, `val0`→`val1`. |
| `ActiveUniverse` | A universe currently fading: its OLA `DmxBuffer`, fetch state, and channel transitions. |
| `FrameValues` | `map<channel_id, value>` — channel values within a universe. |
| `SceneValues` | `map<universe_id, FrameValues>` — all universes within a scene. |

### Threading model

| Thread | Source | Touches | Protected by |
|---|---|---|---|
| OSC listener | `oscreceiver` | parses bundles, appends to `m_scenes` | `m_scenesMutex` |
| RtMidi callback | `mtcreceiver` | decodes MTC, updates atomics | internal to `MtcReceiver` |
| OLA SelectServer | OLA | `processScenes()`, `updateActiveUniverses()`, `SendDMX()` | `m_scenesMutex`, `m_universesMutex` |

`m_scenesMutex` guards the scene queue `m_scenes`; `m_universesMutex` guards
`m_activeUniverses`. The play-head (`playHead`) and connection/run flags are `std::atomic`.

---

[↑ Back to Table of Contents](#table-of-contents)

## Core Concepts

* **Scene transition** — the atomic unit of playback. One top-level OSC bundle yields one
  `SceneTransitionInfo`: a set of target channel values per universe, an MTC start time, and a
  fade duration. Scenes are queued ordered by start time.
* **Play-head** — the current playback position in milliseconds. When following MTC it is
  `estimatedCurrentHead() + output-latency-compensation`; otherwise it is held at `0` and scenes
  apply immediately ("press Go" without timecode).
* **Channel transition** — a per-channel linear interpolation from the channel's current DMX
  value to its target value over the scene's `[mtc_start, mtc_start + fade_time]` window. A zero
  fade time is an instant set.
* **Universe fetch** — before fading a universe, the player asks OLA for that universe's current
  DMX buffer so fades start from the live on-stage value, not from zero. Fetching begins
  `UNIVERSE_FETCH_LOOK_AHEAD_MS` (50 ms) before the scene's start time.
* **Output-latency compensation** — a tunable look-ahead (default 35 ms, range 0–500 ms) added
  to the MTC play-head so DMX frames land on the wire in time with timecode despite OLA, adapter
  and fixture latency.
* **Adaptive timer** — the OLA output callback runs at 10 ms while there is active work and
  drops to 200 ms when idle, cutting CPU ~20×. Incoming scenes wake the timer instantly.
* **Stop-on-MTC-lost** — when timecode disappears, the player either freezes (default) or keeps
  playing (`--ciml`), so a dropout doesn't blackout the stage mid-show.
* **MTC following** — playback only chases timecode when "following" is enabled (`--mtcfollow`
  or OSC `/mtcfollow`); otherwise scenes are applied as soon as they arrive.

---

[↑ Back to Table of Contents](#table-of-contents)

## Design Goals

* **Fail fast on missing infrastructure** — startup probes the OLA daemon and aborts with a
  specific exit code if it is unreachable, rather than silently producing no output.
* **Survive `olad` restarts** — the run loop detects a dropped OLA connection and reconnects
  with exponential backoff (500 ms → 5 s), purging stale scenes so playback resumes cleanly.
* **Start fades from live state** — universes are fetched from OLA before fading so transitions
  begin at the actual on-stage value, never snapping to zero.
* **Be cheap when idle** — the adaptive timer guarantees near-zero CPU between cues while
  keeping sub-frame latency once a scene is queued.
* **Tolerate networked timecode** — MTC timeouts are relaxed for `rtpmidid`/network MIDI so
  normal jitter does not register as a lost signal.
* **Be addressable in a multi-player rig** — every instance carries a `--uuid` that names its
  OLA client and tags its log lines, so many players can run side by side.
* **Never crash on bad input** — out-of-range universes/channels/values are clamped or dropped
  with a warning; constructor failures (e.g. OSC port already bound) exit cleanly with a code.

---

[↑ Back to Table of Contents](#table-of-contents)

## API documentation

`cuems-dmxplayer` exposes three interfaces: the **OSC API** (the primary runtime control
surface), the **command-line interface**, and a small **C++ public API** for embedding the
player. Process **exit codes** are part of the contract with the supervising engine.

### OSC API

The player listens for OSC over UDP on the port given by `--port`. Messages are matched against
the configured address prefix; in the shipped configuration the prefix is **empty**, so the
addresses below are used verbatim.

OSC traffic falls into two groups: **control commands**, accepted at any time, and
**bundle-only messages**, which are only honoured while parsing an OSC bundle.

#### Control commands

| Address | Argument | Effect |
|---|---|---|
| `/quit` | — | Raises `SIGTERM`; the player shuts down gracefully. |
| `/check` | — | Raises `SIGUSR1`; prints/logs the `RUNNING!` status line. |
| `/stoponlost` | — | Toggles the *stop-on-MTC-lost* flag. |
| `/mtcfollow` | `int` *(optional)* | Enables (`≠0`) or disables (`0`) MTC following. With **no** argument, toggles the current state. |
| `/blackout` | — | Clears the scene queue and all active fades, then sends zeros to every active universe. |

#### Bundle-only messages

These are processed **only inside an OSC bundle** and together describe a single scene
transition. A bundle is assembled into one `SceneTransitionInfo` and inserted into the scene
queue ordered by its MTC start time. If no `/mtc_time` or `/start_offset` is supplied, the scene
starts at the current play-head ("now").

| Address | Arguments | Meaning |
|---|---|---|
| `/frame` | `universe_id:int`, then repeating `channel:int value:int` pairs | Target DMX values for a universe. `universe_id` must be `0–65535`; an out-of-range universe makes the whole message ignored. Each `channel` must be `0–512` and `value` `0–255`; out-of-range pairs are skipped with a warning. |
| `/fade_time` | `seconds:float` | Fade duration for the scene, stored internally as `round(1000 × seconds)` milliseconds. |
| `/mtc_time` | `string` | Scene start time. `"now"` → current play-head; `"+<time>"` → play-head **plus** `<time>`; otherwise `max(play-head, <time>)`. `<time>` format is `[[h:]m:]s` (e.g. `90`, `1:30`, `0:01:30`). |
| `/start_offset` | `int` (ms) | Scene start as current play-head **plus** the given millisecond offset. |

**Example** (using `test/send_dmx_osc.py`, which builds bundles with `pyliblo3`):

```python
req = DmxReq()
req.addFrame(1, "FFFFFF")   # universe 1, channels 0..5 → 255
req.send("+0:02", 5.0)      # start 2 s ahead of the play-head, 5 s fade
```

The bundle above contains a `/frame`, a `/mtc_time` (`"+0:02"`), and a `/fade_time` (`5.0`).

### Command-line interface

```
cuems-dmxplayer --port <osc_port> [options]
```

| Option | Alias | Argument | Required | Default | Description |
|---|---|---|---|---|---|
| `--port` | `-p` | `<port>` | **Yes** | — | OSC UDP port to listen on. Must be `1–65535`. |
| `--uuid` | `-u` | `<id>` | No | port number | Unique identifier used for the OLA client name and log slug. |
| `--ciml` | `-c` | — | No | off | *Continue If MTC Lost* — keep playing when the MTC signal drops instead of stopping. |
| `--mtcfollow` | `-m` | — | No | off | Start following MTC immediately, rather than waiting for an OSC `/mtcfollow`. |
| `--output-latency-ms` | — | `<int>` | No | `35` | DMX output-pipeline latency compensation in ms, clamped to `0–500`. Usually fed by the engine from `settings.xml`. |
| `--show` | — | `[w\|c]` | No | — | Print licence disclaimers: `w` = warranty, `c` = copyright; no value prints usage. |

Running with no arguments prints the copyright banner and usage, then exits with
`CUEMS_EXIT_WRONG_PARAMETERS`.

### C++ public API

The `DmxPlayer` class can be embedded directly. Public surface (`dmxplayer.h`):

```cpp
DmxPlayer(int port = 8000,
          const std::string oscRoute = "",
          bool stopOnLostFlag = true,
          bool followMTCFlag = false,
          const std::string& client_name = "DMX_Player");

void run();                       // Connect to OLA and block until final exit
                                  // (handles reconnection internally).
bool IsRunning() const;           // Thread-safe running flag.
void setOutputLatencyMs(long ms); // Set output-latency compensation; clamped to [0, 500].
```

The constructor probes the OLA daemon and calls `exit(CUEMS_EXIT_FAILED_OLA_SETUP)` if it is
unreachable; it may also throw, which `main()` catches and maps to `CUEMS_EXIT_INIT_FAILED`.
`run()` owns the OLA `SelectServer` loop and returns only on a clean shutdown
(`/quit` → `SIGTERM`).

### Exit codes

Defined in `cuems_errors.h`:

| Code | Symbol | Meaning |
|---|---|---|
| `0` | `CUEMS_EXIT_OK` | Success. |
| `-1` | `CUEMS_EXIT_FAILURE` | Generic error. |
| `-2` | `CUEMS_EXIT_WRONG_PARAMETERS` | Invalid/missing command-line parameters. |
| `-3` | `CUEMS_EXIT_WRONG_DATA_FILE` | Invalid data file. |
| `-4` | `CUEMS_EXIT_AUDIO_DEVICE_ERR` | Audio device error (shared code; unused here). |
| `-5` | `CUEMS_EXIT_FAILED_OLA_SETUP` | OLA setup failed — is `olad` running? |
| `-6` | `CUEMS_EXIT_FAILED_OLA_SEL_SERV` | OLA SelectServer failed. |
| `-7` | `CUEMS_EXIT_FAILED_XML_INIT` | XML parser initialisation failed (legacy cue path). |
| `-8` | `CUEMS_EXIT_NO_MIDI_PORTS_FOUND` | No MIDI ports available for MTC. |
| `-9` | `CUEMS_EXIT_INIT_FAILED` | Player constructor failed (e.g. OSC port already bound). |

---

[↑ Back to Table of Contents](#table-of-contents)

## Installation

### Prerequisites

A Linux system with a running **OLA daemon** (`olad`) and the following libraries:

* `librtmidi` — MIDI input for MTC
* `libola`, `libolacommon` — Open Lighting Architecture client
* `libxerces-c` — XML parsing (legacy cue classes)
* `liboscpack` — OSC packet handling (used by the `oscreceiver` submodule)
* `libpthread`, `libstdc++fs` — threading and `std::filesystem`

On Debian/Ubuntu:

```bash
sudo apt-get install -y \
    cmake g++ \
    librtmidi-dev libola-dev libxerces-c-dev liboscpack-dev \
    ola
```

### Build from source

```bash
git clone https://github.com/stagesoft/cuems-dmxplayer.git
cd cuems-dmxplayer
git submodule update --init        # fetch oscreceiver, mtcreceiver, cuemslogger

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install                  # installs cuems-dmxplayer to <prefix>/bin
```

> A legacy `Makefile` also exists and builds the same `cuems-dmxplayer` binary
> (`make` for a debug build with AddressSanitizer, `make release` for an optimised build). CMake
> is the preferred build system.

### Debian package

```bash
git clone --branch debian/bookworm https://github.com/stagesoft/cuems-dmxplayer.git
cd cuems-dmxplayer
dpkg-buildpackage -us -uc
sudo dpkg -i ../cuems-dmxplayer_*.deb
```

---

[↑ Back to Table of Contents](#table-of-contents)

## Usage

Make sure `olad` is running and a DMX output universe is configured (via the OLA web UI at
`http://localhost:9090` or `ola_dev_info`). Then start a player bound to an OSC port:

```bash
# Wait for OSC /mtcfollow before chasing timecode; stop if MTC is lost (defaults)
cuems-dmxplayer --port 8000 --uuid stage-left

# Follow MTC from the start and keep playing if timecode drops out
cuems-dmxplayer --port 8000 --uuid stage-left --mtcfollow --ciml

# Override the output-latency compensation (e.g. for an Art-Net node)
cuems-dmxplayer --port 8000 --output-latency-ms 44
```

Send test scenes with the bundled Python helper (requires `pyliblo3`):

```bash
python3 test/send_dmx_osc.py 8000
```

Trigger a blackout or quit from any OSC client:

```bash
oscsend localhost 8000 /blackout
oscsend localhost 8000 /quit
```

---

[↑ Back to Table of Contents](#table-of-contents)

## Development

```bash
# Debug build with AddressSanitizer + coverage instrumentation
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="--coverage" \
         -DCMAKE_EXE_LINKER_FLAGS="--coverage"
make -j$(nproc)
```

* **Language standard:** C++17 (`-Wall -Wextra`).
* **Submodules:** `oscreceiver`, `mtcreceiver`, `cuemslogger`. Run
  `git submodule update --init` after cloning and re-run it after pulling submodule bumps.
* **Manual testing:** `test/send_dmx_osc.py` sends OSC bundles for end-to-end checks. There is
  currently no automated test suite in this repository; contributions adding one are welcome
  (see [CONTRIBUTORS.md](./CONTRIBUTORS.md)).
* **Logging:** runs through `CuemsLogger` to syslog; the slug is derived from `--uuid`/port.

See [CONTRIBUTORS.md](./CONTRIBUTORS.md) for the full contribution workflow (spec-first, TDD,
DCO sign-off, Conventional Commits, and review requirements).

---

[↑ Back to Table of Contents](#table-of-contents)

## Contributors

Contributions are welcome. The authoritative, step-by-step workflow lives in
[CONTRIBUTORS.md](./CONTRIBUTORS.md); the essentials are summarised here.

**Workflow at a glance**

1. **Spec first (Tier 2).** Non-trivial changes start with a short written spec or issue agreed
   with a maintainer before code. Trivial changes (Tier 1: typos, comments, formatting) can go
   straight to a PR.
2. **Test-driven.** Where automated tests are feasible, add a failing test first, then the
   implementation, then refactor. This repository has no suite yet, so new features that can be
   tested should bring their own (e.g. a `ctest` target).
3. **Branch naming.** `feat/<slug>`, `fix/<slug>`, `chore/<slug>`, `refactor/<slug>`, …
4. **Conventional Commits v1.0.** e.g. `feat(dmxplayer): add /blackout OSC command`. Commit
   bodies should explain the *why* and any breaking impact.
5. **DCO sign-off.** Every commit must be signed off (`git commit -s`) under the
   [Developer Certificate of Origin](https://developercertificate.org/).
6. **SPDX headers.** Every new source file starts with the GPL-3.0 SPDX header.
7. **Pull requests target `main`.** Keep PRs focused; include a clear description, test notes,
   and a CHANGELOG line.

**Maintainers / reviewers**

Every PR is reviewed by one of:

* Ion Reguera — [@ibiltari](https://github.com/ibiltari)
* Adrià Masip — [@backenv](https://github.com/backenv)

**Authorship**

`cuems-dmxplayer` and its submodules are developed and maintained by **Stagelab Coop SCCL**
(historically Stage Lab & bTactic) as part of the CUEMS platform. The `mtcreceiver` submodule
additionally credits Alex Ramos, Ion Reguera, and Adrià Masip.

---

[↑ Back to Table of Contents](#table-of-contents)

## Release notes

See [CHANGELOG.md](./CHANGELOG.md) for the full history. Recent highlights:

* **DMX output-latency compensation.** The MTC play-head is now offset by a configurable
  output latency (default 35 ms, range 0–500 ms) so DMX frames land on the wire in time with
  timecode. Tunable at runtime via `setOutputLatencyMs()` and from the CLI with
  `--output-latency-ms`, which the engine drives from `settings.xml`.
* **Automatic OLA reconnection.** When `olad` restarts or crashes, the player detects the lost
  connection and reconnects with exponential backoff (500 ms → 5 s), purging stale scenes and
  resetting universe state so playback resumes cleanly.
* **Adaptive output timer.** The OLA callback runs at 10 ms while cues are active and drops to
  200 ms when idle (~20× less CPU), instantly waking when a new scene arrives over OSC.
* **`/blackout` OSC command + thread-safety.** A new `/blackout` clears all scenes and fades and
  sends zeros to every universe, guarded by a dedicated `m_universesMutex`. The MTC pipeline
  gained network-tolerant timeouts (`rtpmidid`), an explicit-argument `/mtcfollow`, and a fix
  for a divide-by-zero when `fade_time` is 0.

---

[↑ Back to Table of Contents](#table-of-contents)

## Future developments

The items below are planned but **not yet implemented**. They describe the intended CI/CD and
DevOps integration so the work — and the badges that advertise it — can be wired in
incrementally. Each subsection ends at the badge that proves the pipeline is live.

### 1. Automated test suite (`ctest`)

The current verification path is the manual `test/send_dmx_osc.py` helper. The first milestone is
a real automated suite so behaviour can be guarded in CI:

* Add a `tests/` directory and a `ctest`-driven harness (GoogleTest is the intended framework,
  matching the `mtcreceiver` submodule's `MTCRECV_TESTING` hooks).
* Unit-test the pure logic that does not need live hardware: `DmxPlayer::convertTime()` parsing,
  the fade interpolation in `updateActiveUniverses()` (including the `fade_time = 0` instant-set
  edge case), and OSC argument validation bounds from `CuemsConstants`.
* Gate hardware-dependent paths (OLA `SendDMX`/`FetchDMX`, MTC decode) behind seams so they can
  be exercised without `olad` or a MIDI source, mirroring `mtcreceiver`'s `SkipPortOpenTag` and
  `invokeTickForTesting()` testing affordances.
* Wire the suite into CMake:

```cmake
enable_testing()
add_subdirectory(tests)   # registers ctest targets
```

```bash
ctest --test-dir build --output-on-failure
```

### 2. Tests + coverage workflow (`.github/workflows/tests.yml`)

A GitHub Actions workflow runs the suite on every push to `main` and on every pull request,
builds with coverage instrumentation, and uploads the report to Codecov:

```yaml
name: Tests

on:
  push:
    branches:
      - main
  pull_request:

permissions:
  contents: read

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive   # oscreceiver, mtcreceiver, cuemslogger

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++ lcov \
            librtmidi-dev libola-dev libxerces-c-dev liboscpack-dev

      - name: Configure with coverage
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="--coverage" \
            -DCMAKE_EXE_LINKER_FLAGS="--coverage"

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Run tests
        run: ctest --test-dir build --output-on-failure

      - name: Generate coverage report
        run: |
          lcov --capture --directory build \
               --output-file coverage.info \
               --ignore-errors inconsistent
          lcov --remove coverage.info '/usr/*' '*/tests/*' \
               --output-file coverage.info

      - name: Upload coverage to Codecov
        uses: codecov/codecov-action@v4
        with:
          files: coverage.info
          fail_ci_if_error: false
        env:
          CODECOV_TOKEN: ${{ secrets.CODECOV_TOKEN }}
```

**One-time manual step:** activate the repository at
[codecov.io/gh/stagesoft/cuems-dmxplayer](https://codecov.io/gh/stagesoft/cuems-dmxplayer)
(sign in with GitHub → *Activate*) and add the `CODECOV_TOKEN` repository secret. The first
successful run populates the coverage badge.

Once `tests.yml` exists, append these badges below the existing badge block at the top of this
README:

```markdown
[![Tests](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/tests.yml/badge.svg)](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/tests.yml)
[![Coverage](https://codecov.io/gh/stagesoft/cuems-dmxplayer/graph/badge.svg)](https://codecov.io/gh/stagesoft/cuems-dmxplayer)
```

### 3. Documentation site workflow (`.github/workflows/gh-pages.yml`)

A separate workflow publishes an HTML documentation site to GitHub Pages. For this C++ daemon the
intended toolchain is **MkDocs** (Material theme) for the hand-written architecture/API pages,
optionally fed by **Doxygen** for the class reference extracted from the headers:

```yaml
name: Deploy MkDocs site

on:
  push:
    branches:
      - main

permissions:
  contents: write   # mkdocs gh-deploy pushes to the gh-pages branch

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - run: pip install mkdocs mkdocs-material
      - run: mkdocs gh-deploy --force
```

This requires a minimal `mkdocs.yml` (`site_name: cuems-dmxplayer`,
`repo_url: https://github.com/stagesoft/cuems-dmxplayer`, `markdown_extensions: [admonition]`)
and a `docs/` tree seeded from the *Architecture* and *API documentation* sections of this
README. The published site lives at
[stagesoft.github.io/cuems-dmxplayer](https://stagesoft.github.io/cuems-dmxplayer/).

> **Workflow separation:** keep CI (`tests.yml`) and docs (`gh-pages.yml`) as independent
> workflows. `mkdocs gh-deploy --force` overwrites the `gh-pages` branch, so no other workflow
> should commit to it.

Badge to add once the docs workflow is live:

```markdown
[![Deploy MkDocs site](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/gh-pages.yml/badge.svg)](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/gh-pages.yml)
```

### 4. Debian packaging & release workflow

To complete the DevOps loop and match the rest of the CUEMS ecosystem:

* Maintain a `debian/bookworm` branch with packaging metadata so
  `dpkg-buildpackage -us -uc` produces `cuems-dmxplayer_*.deb` (already referenced under
  [Installation](#installation)).
* Ship a `systemd` unit so the player can be managed as a service
  (`systemctl enable --now cuems-dmxplayer@<uuid>`), with the OSC port and `--output-latency-ms`
  templated per instance.
* Optionally add a release workflow that builds the `.deb` on tag push and attaches it to the
  GitHub Release.

### Target badge set

When all of the above is in place, the badge block at the top of the README should read:

```markdown
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Tests](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/tests.yml/badge.svg)](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/tests.yml)
[![Coverage](https://codecov.io/gh/stagesoft/cuems-dmxplayer/graph/badge.svg)](https://codecov.io/gh/stagesoft/cuems-dmxplayer)
[![Deploy MkDocs site](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/gh-pages.yml/badge.svg)](https://github.com/stagesoft/cuems-dmxplayer/actions/workflows/gh-pages.yml)
```

---

[↑ Back to Table of Contents](#table-of-contents)

## Copyright notice

```
cuems-dmxplayer — Linux DMX player with MTC sync and OSC control for the CUEMS platform.
Copyright (C) 2020-2026 Stagelab Coop SCCL

This program is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If
not, see <https://www.gnu.org/licenses/>.
```

[↑ Back to Table of Contents](#table-of-contents)

## License

`cuems-dmxplayer` is distributed under the terms of the
[GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) (`GPL-3.0-or-later`). The `cuemslogger` and
`oscreceiver` submodules are likewise GPL-3.0; the `mtcreceiver` submodule is `LGPL-3.0-or-later`.
