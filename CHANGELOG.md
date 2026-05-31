<!--
***
SPDX-FileCopyrightText: 2020-2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
***
-->

# Changelog

All notable changes to `cuems-dmxplayer` are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/) loosely; the project version is read from
`CMakeLists.txt` (`project(dmxplayer VERSION 0.0)`).

## v0.0 — 2026-05-31

First documented release. Consolidates the MTC-sync, OLA-resilience, and performance work that
turned the original experimental player into a robust CUEMS daemon.

### Added

- **DMX output-latency compensation.** New `m_outputLatencyMs` atomic on `DmxPlayer` (default
  35 ms — midpoint of ENTTEC wire-DMX ~31 ms and Art-Net network-DMX ~44 ms). The MTC play-head
  is offset by this value in `SendUniverseData()` so DMX fade state lands on the wire in time
  with timecode after the pipeline has latched. Runtime-tunable via `setOutputLatencyMs(long)`,
  clamped to `[0, 500]`.
- **`--output-latency-ms <int>` CLI argument.** Parsed in `main.cpp` and applied after
  construction, before `run()`. Absent → the 35 ms default stands. Intended to be fed by the
  engine from `settings.xml` per node.
- **Automatic OLA reconnection.** When `olad` restarts or crashes, the player detects the lost
  connection and reconnects with exponential backoff (500 ms → 5 s). `OlaClientWrapper` is now a
  `unique_ptr` reconstructed on reconnect; `setupOlaConnection()` / `teardownOlaConnection()`
  were extracted; `run()` wraps `olaServer->Run()` in a reconnect loop. `purgeStaleScenes()`
  discards scenes queued during downtime and resets universe state (pending `FetchDMX` callbacks
  from the dead connection never fire). An `m_running` atomic replaces the old `IsRunning()`
  dependency on the SelectServer.
- **Adaptive output timer.** The OLA callback switches between 10 ms (active) and 200 ms (idle)
  based on whether scenes/transitions are pending — ~20× less CPU when idle. `registerTimer()`,
  `hasActiveWork()`, and `switchToActiveTimer()` were added; a new scene arriving over OSC calls
  `SelectServer::Execute()` to wake the loop instantly with no added latency.
- **`/blackout` OSC command.** Clears the scene queue and all active channel transitions and
  sends zeros to every active universe.
- **`CUEMS_EXIT_INIT_FAILED` (-9).** `DmxPlayer` construction is wrapped in `try/catch` so OSC
  port-bind failures (and any other constructor exception) produce a clean exit instead of an
  unhandled exception / coredump.

### Changed

- **Binary renamed to `cuems-dmxplayer`.** Both build systems (CMake and the legacy Makefile)
  now produce `cuems-dmxplayer`, aligning with the `cuems-<component>` ecosystem convention. The
  internal CMake `project()` name is unchanged.
- **`/mtcfollow` accepts an explicit argument.** `/mtcfollow 1` / `/mtcfollow 0` set the state;
  `/mtcfollow` with no argument keeps the legacy toggle behaviour.
- **Thread-safety for universe state.** A new `m_universesMutex` protects `m_activeUniverses`
  against concurrent access from the OLA callback thread and the OSC receiver thread.
- **Network-tolerant MTC.** `MtcReceiver::setNetworkMode(true)` is enabled so MTC timeouts
  tolerate network jitter (`rtpmidid`) instead of falsely reporting "MTC signal lost".

### Fixed

- **Divide-by-zero on `fade_time = 0`.** `updateActiveUniverses()` now treats a zero-length
  fade as an instant set instead of dividing by a zero interval.
- **Restored `setNetworkMode(true)`** after it was accidentally dropped, which had reintroduced
  50 ms (too-strict) MTC timeouts on networked transports.

### Notes

- Submodules pinned: `mtcreceiver` bumped to its merged `main` tip (thread-safe
  single-fire-per-quarter-frame callback API; `MtcFrame`/full-frame decode fixes); `oscreceiver`
  updated with constructor exception handling and proper destructor cleanup.
- The player consumes only `MtcReceiver::isTimecodeActive()` and `estimatedCurrentHead()`, so the
  `mtcreceiver` v2 callback API change required no source changes here.
- No automated test suite ships yet; `test/send_dmx_osc.py` provides manual end-to-end OSC
  testing.
