<!-- SPDX-FileContributor: Ion Reguera <ion@stagelab.coop> -->

# cuems-dmxplayer

Part of the **CUEMS** ecosystem — see the [`cuems-RELATIONS`](https://github.com/stagesoft/cuems-RELATIONS) repo for the system index, architecture diagram, and protocol/port map.

## Project Overview

cuems-dmxplayer is a Linux DMX lighting player for the CUEMS (Cue Management System) platform. It plays DMX cues synchronized to MIDI Time Code (MTC), receiving scene data and control commands via OSC (Open Sound Control). DMX output goes through OLA (Open Lighting Architecture). The engine→player OSC-bundle protocol is documented once in the cuems-RELATIONS CLAUDE.md ("DMX Cue Lifecycle"); this file covers the player internals. The canonical binary name is **`cuems-dmxplayer`** (see the binary-name gotcha below). Deploy with the **stop → cp → start** discipline.

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

`test/send_dmx_osc.py` is a Python script for sending OSC test messages.

Unit tests (gtest) cover the OLA-free logic in `dmxdayalign.h`: `cmake -DBUILD_TESTS=ON ..`
builds `tests/dmxplayer_tests` (off by default, so the package build is unchanged). Build on
casas, run on a test box (.111), never on casas.

**Absolute `/mtc_time` is converted into this process's MTC day (24 h wrap).** The engine stamps
it in its own extended domain; this long-lived daemon counts the wraps it has seen itself, and the
two differ after a STOP at wire 23–24 h or 0–1 h after a real wrap, or after a node-engine restart
past 24 h. `MtcReceiver::toLocalDay` (nearest day, 12 h rule) runs before `max(playHead, ·)` and the
sorted insert; until the MTC head is authoritative the scene waits in `m_pendingScenes` (never in
`m_scenes` raw) and the OLA tick resolves it. `now`, `+rel` and `/start_offset` are untouched.
Journal: `MTC day-align: on (12 h rule, ext)` at start, `MTC day realign k=…` once per k (WARNING
only for k < 0 with the head still in day 0), implausible start > 1 h ahead, unresolved > 2 s.
Consequence for test tools: a small absolute time (e.g. `send_dmx_osc.py`) sent while the head is
past 12 h is read as the next day. Plan: cuems-RELATIONS
`Plans/2026-10-25-mtc-day-align-24h-wrap.md` step 7.

## Error codes

Defined in `cuems_errors.h`. Exit codes range from 0 (OK) to -9 (init failed). Key ones: `-5` OLA setup failed, `-9` player constructor failed (e.g. port bind error).

## Diagnostics — coordinate transport state

When diagnosing DMX by watching olad `get_dmx?u=0`, **explicitly confirm with the operator whether the project is RUNNING or STOPPED before each measurement** — a watch is only meaningful relative to transport state (phantom-source test → STOPPED; hold test → RUNNING + DMX cue GO'd). dmxplayer's `set channels`/`holding` lines are **stdout** → they land in the node-engine "Subprocess output" journal, NOT the `Cuems:d<uuid>` syslog slug (that carries CuemsLogger MTC events). `strace` is often NOT installed on rigs; use `tcpdump` + `ss -tni` byte counters + `kill -USR1 olad` (live DEBUG) instead.

## Field notes / gotchas (DMX-dead failure modes)

These are distinct root causes for the same symptom (video/audio fine, DMX universe flat 0):

- **Doubled MTC quarter-frames → `isTimecodeActive()` never true.** If `MtcMaster` is subscribed to `Midi Through Port-0` twice (`aconnect -l` shows `14:0, 14:0[real:0]`), every QF is delivered doubled (200/s at 25fps) → `mtcreceiver` never assembles a complete 8-QF sequence → `timecodeRunWeight` stays 0 → the DMX gate never opens. Videocomposer rides the per-QF tick so it's fine (that divergence is the tell). Root cause: cuems-midi-connector's plain ALSA subscription doesn't dedupe against RtMidi's real-time-flagged one (kernel `match_subs_info` only dedupes matching flags). Immediate fix: `sudo aconnect -d 130:0 14:0`. Durable fix lives in cuems-midi-connector (real-time flags on the `to_through` subscribe). Preventing it at libmtcmaster `openPort` proved unreliable (it's a connect-time race) — only post-connect dedup works.
- **Cold-boot MIDI openPort race → player dies/hangs.** cuems-midi-connector wires `14:0 → player recv` at the same instant the player's own `MtcReceiver::openPort(0)` self-subscribes; at cold boot midi-connector wins → the player's openPort gets EBUSY → RtMidi throws → dmxplayer dies (node-engine does not respawn it). Tell: `ls /proc/<pid>/fd | grep snd` empty. Fixed by **deferring `from_through` wiring 1s in cuems-midi-connector** (commit `9af92bb`, scoped to the new-client announce path only via `CUEMS_MIDICONN_FROM_THROUGH_GRACE_S`) so the player self-wires first; dmxplayer `main.cpp` also test-opens port 0 before constructing (`af7e8fa`, env `CUEMS_DMX_MIDI_WAIT_S`) as defense-in-depth.
- **olad stale-prune resetting the universe.** dmxplayer pushes a static look to olad once and never refreshes; olad's garbage collector (`K_HOUSEKEEPING_TIMEOUT_MS`=10s) prunes a source that stops sending. The stagesoft OLA fork **RETAINS** the last buffer on prune (does not zero it), so the keep-alive (`fix/dmxplayer-hold-output` `9d0644a`, `refreshHeldUniverses` every 5s) was found unnecessary and **reverted for release** — kept on its branch. The real "~60s DMX blip" was ultimately a **hidden browser tab** on the OLA web UI (`:9090`) POSTing 0-value frames as a transient DMX source. Lesson: when a universe gets stray frames, check for an OLA web-UI tab.
- **Binary-name mismatch → "No local DMX player available".** A `.deb` once shipped the binary as `/usr/bin/dmxplayer-cuems` (reversed) while the engine spawns `/usr/bin/cuems-dmxplayer` (from `settings.xml <dmxplayer><path>`) → `[Errno 2]` → player thread dies → every DmxCue arms with the error. Current source (`CMakeLists add_executable(cuems-dmxplayer ...)`, debian staging `usr/bin/cuems-dmxplayer`) builds the canonical name. The repo was unified onto `main` (0.0.0-6) so one branch builds the correctly-named `.deb` — redeploy fleet-wide; older clusters may still run the mis-named one.
- OLA setup (rogue-olad eviction, Open-DMX / Eurolite MK2 patching, universe **0** not 1): see the `ola` fork's CLAUDE.md.

