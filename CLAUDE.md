# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

cuems-dmxplayer is a Linux DMX lighting player for the CUEMS (Cue Management System) platform. It plays DMX cues synchronized to MIDI Time Code (MTC), receiving scene data and control commands via OSC (Open Sound Control). DMX output goes through OLA (Open Lighting Architecture).

## Build

Two build systems exist; CMake is the primary one:

```bash
# CMake (preferred)
mkdir -p build && cd build
cmake ..
make

# Legacy Makefile (includes address sanitizer by default)
make          # debug build
make release  # optimized build
```

**Dependencies:** librtmidi, libola, libolacommon, libxerces-c, libpthread, libstdc++fs. The Makefile also links liboscpack directly.

**C++ standard:** C++17

**Submodules:** `oscreceiver/`, `mtcreceiver/`, and `cuemslogger/` are git submodules. Run `git submodule update --init` after cloning.

## Architecture

**DmxPlayer** (`dmxplayer.h/cpp`) is the central class. It inherits from `OscReceiver` (submodule) to receive OSC messages and owns an `MtcReceiver` (submodule) for MIDI timecode tracking. It uses OLA's `OlaClientWrapper` and `SelectServer` for DMX output.

### Data flow

1. OSC bundles arrive containing `/frame` (channel values per universe), `/fade_time`, and `/mtc_time` or `/start_offset` messages. These are assembled into `SceneTransitionInfo` structs during `ProcessBundle`.
2. `SendUniverseData` is called on a 10ms OLA timer. It updates `playHead` from MTC (or holds at 0 when not following MTC), then calls `processScenes` → `updateActiveUniverses`.
3. `processScenes` converts pending scenes into per-channel fade transitions (`ChannelTransition`) once the universe's current DMX state has been fetched from OLA.
4. `updateActiveUniverses` interpolates each channel between `val0`→`val1` over `mtc0`→`mtc1`, sends the buffer to OLA via `SendDMX`, and removes completed transitions.

### OSC commands (address prefix = oscAddress)

- `/quit` — SIGTERM
- `/check` — SIGUSR1 (prints RUNNING status)
- `/stoponlost` — toggles stop-on-MTC-lost
- `/mtcfollow [0|1]` — enable/disable MTC following (toggle if no arg)
- `/blackout` — clears all scenes, fades, and sends zeros

Bundle-only messages: `/frame`, `/fade_time`, `/mtc_time`, `/start_offset`

### Legacy XML cue system

`DmxCue_v0`/`DmxCue_v1` (`dmxcue_v*.h/cpp`) parse DMX scene XML files using Xerces-C. This is the older cue-loading path; the current runtime path uses OSC bundles.

### Key types (in DmxPlayer)

- `SceneTransitionInfo` — holds target values, MTC start time, and fade duration
- `ChannelTransition` — per-channel linear interpolation state (mtc0/mtc1, val0/val1)
- `ActiveUniverse` — tracks OLA DmxBuffer and pending channel transitions per universe

### Threading

OSC messages arrive on the OscReceiver listener thread. MTC is decoded on the RtMidi callback thread. Scene processing and DMX output run on the OLA SelectServer thread. `m_scenesMutex` protects `m_scenes`; `m_universesMutex` protects `m_activeUniverses`.

## Running

```bash
dmxplayer --port <osc_port> [--uuid <id>] [--ciml] [--mtcfollow]
```

- `--port` / `-p` (required): OSC listen port
- `--uuid` / `-u`: unique ID for OLA client naming and logging
- `--ciml` / `-c`: continue playing if MTC signal is lost
- `--mtcfollow` / `-m`: start following MTC immediately (default: wait for OSC `/mtcfollow`)

Requires OLA daemon (`olad`) running.

## Testing

`test/send_dmx_osc.py` is a Python script for sending OSC test messages. No automated test suite exists.

## Error codes

Defined in `cuems_errors.h`. Exit codes range from 0 (OK) to -9 (init failed). Key ones: `-5` OLA setup failed, `-9` player constructor failed (e.g. port bind error).
