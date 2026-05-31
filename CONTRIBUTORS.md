<!--
***
SPDX-FileCopyrightText: 2020-2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
***
-->

# Contributing to cuems-dmxplayer

Thank you for contributing to `cuems-dmxplayer`, the DMX lighting playback daemon for the CUEMS
platform. This document is the authoritative guide to how changes are proposed, written,
reviewed, and merged. Read it before opening a pull request.

The non-negotiable invariants are: a **spec first** for non-trivial work, **test-driven
development** where automated testing is feasible, **DCO sign-off** on every commit, PRs that
target **`main`**, and an **SPDX header** on every new source file.

---

## 1. Prerequisites

| Tool | Minimum version | Purpose |
|---|---|---|
| `cmake` | ≥ 3.20 | Primary build system |
| `g++` / `gcc` | ≥ 12 (C++17) | Compiler |
| `git` | ≥ 2.30 | Version control + submodules |
| `clang-format` | ≥ 14 *(optional)* | Style checking |
| `clang-tidy` | ≥ 14 *(optional)* | Static analysis |
| `lcov` | ≥ 1.15 *(optional)* | Coverage reports |

Runtime/build libraries: `librtmidi`, `libola`, `libolacommon`, `libxerces-c`, `liboscpack`,
`libpthread`, `libstdc++fs`. A running `olad` is required to exercise DMX output, and a MIDI
Time Code source (hardware or `rtpmidid`) to exercise MTC sync.

---

## 2. Development Setup

```bash
git clone https://github.com/stagesoft/cuems-dmxplayer.git
cd cuems-dmxplayer
git submodule update --init        # oscreceiver, mtcreceiver, cuemslogger

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

For a sanitised debug build (the legacy `Makefile` enables AddressSanitizer by default):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
         -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
make -j$(nproc)
```

Manual end-to-end check:

```bash
olad &                              # start OLA if not already running
./build/cuems-dmxplayer --port 8000 --mtcfollow &
python3 test/send_dmx_osc.py 8000   # requires pyliblo3
```

---

## 3. Contribution Tiers

| Tier | Examples | Process |
|---|---|---|
| **Tier 1 — trivial** | Typos, comments, formatting, doc fixes, log-message wording | Open a PR directly. No spec required. |
| **Tier 2 — non-trivial** | New OSC commands, behaviour changes, timing/threading changes, new CLI flags, dependency changes | **Spec first** (§5), then TDD (§6), then PR. |

If in doubt, treat the change as Tier 2.

---

## 4. Branch Naming

Branch from `main` using a typed slug:

```
feat/<slug>       e.g. feat/sacn-priority-flag
fix/<slug>        e.g. fix/fade-time-zero-divide
chore/<slug>      e.g. chore/bump-mtcreceiver
refactor/<slug>   e.g. refactor/scene-queue
docs/<slug>       e.g. docs/osc-api-table
```

---

## 5. Spec-First Requirement

For Tier 2 changes, write a short spec **before** writing code and agree it with a maintainer
(in the issue or PR description). A spec states:

* **What** changes (the observable behaviour, the OSC/CLI surface, the invariant).
* **Why** it is needed.
* **How** correctness will be demonstrated (test, manual procedure, or measurement).
* **Risk** to threading, timing, or the OLA/MTC contract.

This is especially important here because the player runs three cooperating threads around
shared state; behavioural changes must state which thread touches what and under which mutex.

---

## 6. TDD Workflow — Non-Negotiable

Where a change is testable in isolation, follow the sequence:

1. **Red** — write a failing test that captures the desired behaviour.
2. **Green** — write the minimum implementation to pass it.
3. **Refactor** — clean up with the test still green.

This repository does not yet ship an automated suite. If your change is unit-testable
(e.g. time conversion, interpolation math, OSC argument validation), add a `ctest` target and
the corresponding test rather than relying solely on manual checks. Logic that requires live
OLA/MTC hardware must include a documented manual test procedure in the PR.

---

## 7. Commit Hygiene (Conventional Commits v1.0)

Use [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):

```
<type>(<scope>): <subject>

<body — the why, and any breaking impact>
```

* **Types:** `feat`, `fix`, `chore`, `refactor`, `docs`, `test`, `perf`, `build`.
* **Scopes** seen in this repo: `dmxplayer`, `dmx`, `mtcreceiver`, `osc`.
* Breaking changes: add `!` after the scope or a `BREAKING CHANGE:` footer.

Examples from the history:

```
feat(dmx): add /blackout OSC command to clear fades and send zeros
fix: catch player constructor exceptions and add CUEMS_EXIT_INIT_FAILED
chore(dmxplayer): bump mtcreceiver to post-Phase-2 tip
```

---

## 8. Developer Certificate of Origin (DCO)

Every commit must be signed off, certifying you wrote the change or have the right to submit it
under the project licence:

```bash
git commit -s -m "feat(dmx): ..."
```

This appends `Signed-off-by: Your Name <you@example.com>`. See
[developercertificate.org](https://developercertificate.org/). Unsigned commits will be asked to
amend before merge.

---

## 9. Pull Request Requirements

A PR must:

* Target the **`main`** branch.
* Be focused on a single concern; split unrelated changes.
* Include a description: what, why, and how it was verified.
* Note any submodule bumps and the commit they point to.
* Keep submodule pointers intentional — never commit an accidental submodule downgrade.
* Pass review by a maintainer (§11).
* Include a CHANGELOG line (§12) and an SPDX header on any new file.

---

## 10. Acceptance Criteria

Before requesting review, confirm:

| Gate | Command / check |
|---|---|
| Builds clean | `cmake .. && make -j$(nproc)` with no new warnings (`-Wall -Wextra`) |
| Sanitizers clean | Debug build with `-fsanitize=address,undefined` runs the affected path without reports |
| Style | `clang-format --dry-run --Werror <files>` (if configured) |
| Static analysis | `clang-tidy <files>` raises no new issues (if configured) |
| Tests | Any added `ctest` targets pass (`ctest --output-on-failure`) |
| Manual | Documented manual procedure for hardware-dependent paths (OLA/MTC) |
| Submodules | `git submodule status` reflects intended pointers |

---

## 11. Review Process

Every PR is reviewed by one of the maintainers:

* Ion Reguera — [@ibiltari](https://github.com/ibiltari)
* Adrià Masip — [@backenv](https://github.com/backenv)

Reviewers check correctness, thread-safety, the OSC/CLI contract, and that the change matches its
spec. Address review comments with follow-up commits (don't force-push over a review in
progress unless asked); the branch is squashed/merged once approved.

---

## 12. Changelog Line

Add an entry under the current unreleased section of [CHANGELOG.md](./CHANGELOG.md) in the
appropriate group (`Added`, `Changed`, `Fixed`, `Removed`, `Notes`). Describe the behaviour and
why it matters — not just the file touched. For breaking changes, include a migration note.

---

## 13. Dependency Governance

* New libraries must be justified in the spec and available as Debian packages where possible
  (the project targets Debian/bookworm).
* Add new dependencies to `CMakeLists.txt` (and the legacy `Makefile` if it must keep building)
  and document them in the README's *Prerequisites* section.
* Submodule bumps (`oscreceiver`, `mtcreceiver`, `cuemslogger`) are `chore` commits that name
  the target submodule commit and summarise what the bump pulls in, including any breaking API
  changes and why this player is (or isn't) affected.
* Prefer system OLA/RtMidi/Xerces packages over vendoring.

---

## 14. License

`cuems-dmxplayer` is licensed under the **GNU General Public License v3.0 or later**
(`GPL-3.0-or-later`). By contributing, you agree that your contributions are licensed under the
same terms.

Every new source file must begin with an SPDX header. For C/C++:

```cpp
// SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
// SPDX-License-Identifier: GPL-3.0-or-later
```

For Markdown, wrap it in an HTML comment as at the top of this file.
