/*
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
 *
 * Day alignment of absolute /mtc_time values (24h MTC wrap).
 *
 * The engine sends an absolute /mtc_time in ITS 24h-extended MTC domain (the
 * wraps it has seen since its last reset); this process's MTC head counts the
 * wraps THIS process has seen. They differ after a STOP at wire 23-24 h or
 * 0-1 h after a real wrap, or when the node engine restarts past 24 h while
 * this daemon keeps running. Every absolute engine time is converted into this
 * process's day with MtcReceiver::toLocalDay() before it is compared with the
 * play head or sorted; `now`, `+rel` and /start_offset are relative and stay
 * as they are.
 *
 * Pure helpers, testable without OLA (tests/test_dayalign.cpp). Plan:
 * cuems-RELATIONS Plans/2026-10-25-mtc-day-align-24h-wrap.md, step 7.
 */

#ifndef DMXDAYALIGN_H
#define DMXDAYALIGN_H

#include <algorithm>
#include <charconv>
#include <cmath>
#include <list>
#include <string>
#include <string_view>
#include "mtcreceiver.h"

namespace DmxDayAlign {

constexpr long HOUR_MS = 3600000L;
constexpr long UNRESOLVED_WARN_MS = 2000L;

// Time format: [[h:]m:]s  (s may carry decimals). Hours may exceed 23: the
// engine's extended domain. Unchanged from DmxPlayer::convertTime.
inline long convertTime(std::string_view time)
{
    double seconds = 0;
    int minutes = 0;
    int hours = 0;
    const char* p = time.data();

    int L = time.size() - 1;
    auto j = time.rfind(':', L) + 1;
    std::from_chars(p + j, p + L + 1, seconds);
    if (1 < j) {
        L = j - 2;
        j = time.rfind(':', L) + 1;
        std::from_chars(p + j, p + L + 1, minutes);
    }
    if (1 < j) {
        L = j - 2;
        j = time.rfind(':', L) + 1;
        std::from_chars(p + j, p + L + 1, hours);
    }
    return std::round(seconds * 1000 + 60 * 1000 * minutes + 3600 * 1000 * hours);
}

// /mtc_time "now" | "+rel" | absolute.
enum class TimeKind { Now, Relative, Absolute };
struct MtcTime {
    TimeKind kind = TimeKind::Now;
    long ms = 0;        // Relative: the delta; Absolute: the engine time
};

inline MtcTime parseMtcTime(std::string_view s)
{
    if (s == "now") return {TimeKind::Now, 0};
    if (!s.empty() && s[0] == '+') return {TimeKind::Relative, convertTime(s.substr(1))};
    return {TimeKind::Absolute, convertTime(s)};
}

// An absolute engine time into this process's day, never before the play head
// (as before: a start in the past fires now). False while the head is not
// authoritative: the caller keeps the scene pending. toLocalDay is
// bool(long engineMs, long& localMs, long& k): MtcReceiver::toLocalDay in the
// player, a fake in the tests.
template <typename ToLocalDay>
bool resolveAbsolute(long engineMs, long playHead, ToLocalDay&& toLocalDay, long& startMs, long& k)
{
    long localMs = 0;
    if (!toLocalDay(engineMs, localMs, k)) return false;
    startMs = std::max(playHead, localMs);
    return true;
}

// The list stays sorted by m_mtcStart; a scene goes after the ones with an
// equal start (arrival order kept), as the bundle path always did.
template <typename Scene>
void insertSorted(std::list<Scene>& scenes, Scene&& scene)
{
    auto r_it = scenes.rbegin();
    for (; r_it != scenes.rend(); ++r_it) {
        if (r_it->m_mtcStart <= scene.m_mtcStart) break;
    }
    scenes.insert(r_it.base(), std::move(scene));
}

// A scene waiting for an authoritative head: never in the sorted list raw.
template <typename Scene>
struct Pending {
    Scene scene;
    long engineMs = 0;
    long sinceMs = 0;       // steady-clock ms when it was queued
    bool warned = false;    // the "unresolved" warning, once per scene
};

// What the caller logs after a drain (it holds no lock while logging).
struct DrainReport {
    int resolved = 0;
    int stillPending = 0;
    long lastK = 0;
    bool anyShifted = false;            // a resolution with k != 0
    long shiftedEngineMs = 0;
    int newlyUnresolved = 0;            // pending > UNRESOLVED_WARN_MS for the first time
    long oldestPendingMs = 0;
    int implausible = 0;                // resolved starts more than an hour ahead
    long implausibleStartMs = 0;
};

// A start more than an hour ahead of the play head means a wrong day (or an
// engine time far from now): worth one WARNING.
inline bool implausibleStart(long startMs, long playHead) { return startMs > playHead + HOUR_MS; }

// Move every pending scene the head can now place into the sorted list.
// `mtcRunning`: the unresolved warning only counts while MTC is running.
template <typename Scene, typename ToLocalDay>
DrainReport drainPending(std::list<Pending<Scene>>& pending, std::list<Scene>& scenes,
                         long playHead, long nowMs, bool mtcRunning, ToLocalDay&& toLocalDay)
{
    DrainReport rep;
    for (auto it = pending.begin(); it != pending.end(); ) {
        long startMs = 0, k = 0;
        if (resolveAbsolute(it->engineMs, playHead, toLocalDay, startMs, k)) {
            it->scene.m_mtcStart = startMs;
            if (k != 0) { rep.anyShifted = true; rep.lastK = k; rep.shiftedEngineMs = it->engineMs; }
            if (implausibleStart(startMs, playHead)) { rep.implausible++; rep.implausibleStartMs = startMs; }
            insertSorted(scenes, std::move(it->scene));
            it = pending.erase(it);
            rep.resolved++;
            continue;
        }
        rep.stillPending++;
        if (mtcRunning && !it->warned && nowMs - it->sinceMs > UNRESOLVED_WARN_MS) {
            it->warned = true;
            rep.newlyUnresolved++;
            rep.oldestPendingMs = std::max(rep.oldestPendingMs, nowMs - it->sinceMs);
        }
        ++it;
    }
    return rep;
}

// This daemon outlives node-engine restarts, so a negative k is expected once
// it has seen a wrap of its own (head in day >= 1). With the head still in day
// 0, nothing explains the engine being a day behind.
inline bool realignIsWarning(long k, long headMs) { return k < 0 && headMs < MtcReceiver::DAY_MS; }

inline std::string realignLine(long k, long engineMs, long headMs)
{
    return "MTC day realign k=" + std::to_string(k) + " (engine " + std::to_string(engineMs)
           + " ms, local head " + std::to_string(headMs) + " ms, phase=ext)";
}

} // namespace DmxDayAlign

#endif // DMXDAYALIGN_H
