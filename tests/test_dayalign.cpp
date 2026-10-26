/*
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
 *
 * Day alignment of absolute /mtc_time values (dmxdayalign.h): parsing, the
 * conversion into this process's MTC day, the pending queue and the sorted
 * scene list. DmxPlayer itself needs OLA; its decisions live in these helpers.
 */

#include <gtest/gtest.h>
#include <list>
#include "dmxdayalign.h"

namespace {

constexpr long DAY = MtcReceiver::DAY_MS;
constexpr long H = 3600000L;

struct Scene {
    long m_mtcStart = 0;
    int id = 0;
};

// A head this test controls, through the same signature as MtcReceiver::toLocalDay.
struct FakeHead {
    bool authoritative = true;
    long head = 0;
    bool operator()(long engineMs, long& localMs, long& k) const {
        if (!authoritative) return false;
        k = MtcReceiver::dayShift(engineMs, head);
        localMs = engineMs - k * DAY;
        return true;
    }
};

bool sortedStable(const std::list<Scene>& s) {
    long prev = -1;
    for (const auto& x : s) {
        if (x.m_mtcStart < prev) return false;
        prev = x.m_mtcStart;
    }
    return true;
}

} // namespace

TEST(DmxDayAlign, ConvertTimeUnchanged) {
    EXPECT_EQ(DmxDayAlign::convertTime("5"), 5000);
    EXPECT_EQ(DmxDayAlign::convertTime("0.04"), 40);
    EXPECT_EQ(DmxDayAlign::convertTime("1:02"), 62000);
    EXPECT_EQ(DmxDayAlign::convertTime("1:02:03.5"), 3723500);
    EXPECT_EQ(DmxDayAlign::convertTime("25:00:00"), 90000000) << "the engine's extended domain";
}

TEST(DmxDayAlign, ParseKinds) {
    EXPECT_EQ(DmxDayAlign::parseMtcTime("now").kind, DmxDayAlign::TimeKind::Now);
    const auto rel = DmxDayAlign::parseMtcTime("+2.5");
    EXPECT_EQ(rel.kind, DmxDayAlign::TimeKind::Relative);
    EXPECT_EQ(rel.ms, 2500);
    const auto abs = DmxDayAlign::parseMtcTime("24:05:07.68");
    EXPECT_EQ(abs.kind, DmxDayAlign::TimeKind::Absolute);
    EXPECT_EQ(abs.ms, DAY + 307680);
}

TEST(DmxDayAlign, ResolveAlignedIsUnchanged) {
    FakeHead f{true, 13 * H};
    long start = 0, k = 7;
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(13 * H + 5000, 13 * H, f, start, k));
    EXPECT_EQ(k, 0);
    EXPECT_EQ(start, 13 * H + 5000);
    // a start already past fires now, as before: max(playHead, start)
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(13 * H - 5000, 13 * H, f, start, k));
    EXPECT_EQ(start, 13 * H);
}

// S3: a STOP at wire 23-24 h; this daemon's day count and the node engine's differ.
TEST(DmxDayAlign, ResolveEngineAheadByADay) {
    FakeHead f{true, 600000};                         // this process: day 0 + 00:10:00
    long start = 0, k = 0;
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(DAY + 605000, 600000, f, start, k));
    EXPECT_EQ(k, 1);
    EXPECT_EQ(start, 605000) << "5 s ahead, not 24 h";
}

// The daemon alive across midnight, the node engine restarted (engine in day 0).
TEST(DmxDayAlign, ResolveEngineBehindByADay) {
    FakeHead f{true, DAY + 600000};
    long start = 0, k = 0;
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(605000, DAY + 600000, f, start, k));
    EXPECT_EQ(k, -1);
    EXPECT_EQ(start, DAY + 605000) << "5 s ahead, not fired 24 h late as 'past'";
}

TEST(DmxDayAlign, NotAuthoritativeLeavesOutputsAlone) {
    FakeHead f{false, 0};
    long start = -7, k = -7;
    EXPECT_FALSE(DmxDayAlign::resolveAbsolute(DAY + 605000, 0, f, start, k));
    EXPECT_EQ(start, -7);
    EXPECT_EQ(k, -7);
}

TEST(DmxDayAlign, InsertSortedKeepsOrderAndArrival) {
    std::list<Scene> s;
    const long starts[] = {500, 100, 300, 300, 900, 100, 0};
    int id = 0;
    for (long t : starts) DmxDayAlign::insertSorted(s, Scene{t, id++});
    EXPECT_TRUE(sortedStable(s));
    // equal starts keep arrival order: the second 100 (id 5) after the first (id 1)
    std::vector<int> ids;
    for (const auto& x : s) ids.push_back(x.id);
    EXPECT_EQ(ids, (std::vector<int>{6, 1, 5, 2, 3, 0, 4}));
}

TEST(DmxDayAlign, PendingWaitsThenGoesInSorted) {
    std::list<Scene> scenes;
    DmxDayAlign::insertSorted(scenes, Scene{600000, 100});
    DmxDayAlign::insertSorted(scenes, Scene{620000, 101});
    std::list<DmxDayAlign::Pending<Scene>> pending;
    pending.push_back({Scene{0, 1}, DAY + 610000, 1000, false});
    pending.push_back({Scene{0, 2}, DAY + 605000, 1000, false});
    FakeHead f{false, 600000};

    auto rep = DmxDayAlign::drainPending(pending, scenes, 600000, 1500, true, f);
    EXPECT_EQ(rep.resolved, 0);
    EXPECT_EQ(rep.stillPending, 2);
    EXPECT_EQ(rep.newlyUnresolved, 0) << "not yet 2 s";
    EXPECT_EQ(scenes.size(), 2u) << "never in the sorted list raw";

    rep = DmxDayAlign::drainPending(pending, scenes, 600000, 3100, true, f);
    EXPECT_EQ(rep.newlyUnresolved, 2);
    EXPECT_EQ(rep.oldestPendingMs, 2100);
    rep = DmxDayAlign::drainPending(pending, scenes, 600000, 4000, true, f);
    EXPECT_EQ(rep.newlyUnresolved, 0) << "once per scene";

    f.authoritative = true;
    rep = DmxDayAlign::drainPending(pending, scenes, 600000, 4100, true, f);
    EXPECT_EQ(rep.resolved, 2);
    EXPECT_TRUE(pending.empty());
    ASSERT_EQ(rep.shifts.size(), 1u);
    EXPECT_EQ(rep.shifts[0].first, 1);
    EXPECT_TRUE(sortedStable(scenes));
    std::vector<int> ids;
    for (const auto& x : scenes) ids.push_back(x.id);
    EXPECT_EQ(ids, (std::vector<int>{100, 2, 1, 101})) << "605000 and 610000 between the two";
}

TEST(DmxDayAlign, NoUnresolvedWarningWithoutMtc) {
    std::list<Scene> scenes;
    std::list<DmxDayAlign::Pending<Scene>> pending;
    pending.push_back({Scene{0, 1}, 5000, 0, false});
    FakeHead f{false, 0};
    auto rep = DmxDayAlign::drainPending(pending, scenes, 0, 60000, false, f);
    EXPECT_EQ(rep.newlyUnresolved, 0) << "MTC stopped: waiting is expected";
    EXPECT_EQ(rep.stillPending, 1);
}

// now / +rel / start_offset never go through the conversion: relative to the
// play head whatever its day, never pending even with no authoritative head.
TEST(DmxDayAlign, BundleNowAndRelativeNeverConvertNorQueue) {
    FakeHead noHead{false, 0};
    DmxDayAlign::BundleStart b;
    b.begin(13 * H);
    EXPECT_FALSE(b.pending);
    EXPECT_EQ(b.startMs, 13 * H) << "a bundle with no /mtc_time starts now";
    b.mtcTime("+2", 13 * H, noHead);
    EXPECT_FALSE(b.pending);
    EXPECT_EQ(b.startMs, 13 * H + 2000);
    b.mtcTime("now", 13 * H, noHead);
    EXPECT_FALSE(b.pending);
    EXPECT_EQ(b.startMs, 13 * H);
    b.startOffset(13 * H, 1500);
    EXPECT_FALSE(b.pending);
    EXPECT_EQ(b.startMs, 13 * H + 1500);
}

// The bundle state across messages: a later relative time clears a pending
// absolute one; flush puts the scene in exactly one of the two lists.
TEST(DmxDayAlign, BundleFlushGoesToExactlyOneList) {
    FakeHead f{false, 600000};
    std::list<Scene> scenes;
    std::list<DmxDayAlign::Pending<Scene>> pending;
    std::size_t dropped = 0;

    DmxDayAlign::BundleStart b;
    b.begin(600000);
    b.mtcTime("24:10:05", 600000, f);                 // no authoritative head: pending
    ASSERT_TRUE(b.pending);
    EXPECT_EQ(b.engineMs, DAY + 605000);
    DmxDayAlign::flush(Scene{0, 1}, b, scenes, pending, 1000, dropped);
    EXPECT_TRUE(scenes.empty()) << "never in the sorted list raw";
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending.front().engineMs, DAY + 605000);

    b.begin(600000);
    b.mtcTime("24:10:05", 600000, f);
    b.mtcTime("+1", 600000, f);                       // a later message in the same bundle
    EXPECT_FALSE(b.pending);
    DmxDayAlign::flush(Scene{0, 2}, b, scenes, pending, 1000, dropped);
    ASSERT_EQ(scenes.size(), 1u);
    EXPECT_EQ(scenes.front().m_mtcStart, 601000);
    EXPECT_EQ(pending.size(), 1u);

    f.authoritative = true;
    b.begin(600000);
    b.mtcTime("24:10:05", 600000, f);                 // authoritative: converted at once
    EXPECT_FALSE(b.pending);
    EXPECT_EQ(b.k, 1);
    EXPECT_EQ(b.startMs, 605000);
    DmxDayAlign::flush(Scene{0, 3}, b, scenes, pending, 1000, dropped);
    EXPECT_TRUE(sortedStable(scenes));
    EXPECT_EQ(dropped, 0u);
}

TEST(DmxDayAlign, PendingQueueIsCapped) {
    FakeHead f{false, 0};
    std::list<Scene> scenes;
    std::list<DmxDayAlign::Pending<Scene>> pending;
    std::size_t dropped = 0;
    DmxDayAlign::BundleStart b;
    for (int i = 0; i < (int)DmxDayAlign::MAX_PENDING + 5; ++i) {
        b.begin(0);
        b.mtcTime("1:00:00", 0, f);
        DmxDayAlign::flush(Scene{0, i}, b, scenes, pending, i, dropped);
    }
    EXPECT_EQ(pending.size(), DmxDayAlign::MAX_PENDING);
    EXPECT_EQ(dropped, 5u);
    EXPECT_EQ(pending.front().scene.id, 5) << "the oldest are the ones dropped";
}

// One drain resolving scenes of two different days reports both k values.
TEST(DmxDayAlign, DrainReportsEveryDistinctK) {
    std::list<Scene> scenes;
    std::list<DmxDayAlign::Pending<Scene>> pending;
    pending.push_back({Scene{0, 1}, DAY + 605000, 0, false});   // k = 1
    pending.push_back({Scene{0, 2}, DAY + 606000, 0, false});   // k = 1 again
    pending.push_back({Scene{0, 3}, 605000 - DAY, 0, false});   // k = -1
    FakeHead f{true, 600000};
    const auto rep = DmxDayAlign::drainPending(pending, scenes, 600000, 0, true, f);
    EXPECT_EQ(rep.resolved, 3);
    ASSERT_EQ(rep.shifts.size(), 2u);
    EXPECT_EQ(rep.shifts[0].first, 1);
    EXPECT_EQ(rep.shifts[1].first, -1);
}

// Documented consequence: a small absolute time sent by a test tool
// (test/send_dmx_osc.py) while the head is past 12 h is read as the next day.
TEST(DmxDayAlign, SmallAbsoluteTimePastTwelveHoursIsNextDay) {
    FakeHead f{true, 13 * H};
    long start = 0, k = 0;
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(5000, 13 * H, f, start, k));
    EXPECT_EQ(k, -1);
    EXPECT_EQ(start, DAY + 5000);
}

// Rig 2026-10-26 (test2, S3 run): a scene sent before /mtcfollow, while the
// play head is held at 0, was reported implausible against that 0. The check
// compares with the MTC head instead.
TEST(DmxDayAlign, ImplausibleUsesTheMtcHeadNotTheHeldPlayHead) {
    FakeHead f{true, 84600000L};                                // head 23:30:00
    DmxDayAlign::BundleStart b;
    b.begin(0);
    b.mtcTime("23:30:06.6", 0, f, 84600000L);                   // play head held at 0
    EXPECT_FALSE(b.pending);
    EXPECT_FALSE(b.implausible) << "6.6 s ahead of the MTC head";
    b.mtcTime("23:30:06.6", 0, f);                              // without the head: the old check
    EXPECT_TRUE(b.implausible);

    std::list<Scene> scenes;
    std::list<DmxDayAlign::Pending<Scene>> pending;
    pending.push_back({Scene{0, 1}, 84606600L, 0, false});
    const auto rep = DmxDayAlign::drainPending(pending, scenes, 0, 0, true, f, 84600000L);
    EXPECT_EQ(rep.resolved, 1);
    EXPECT_EQ(rep.implausible, 0);
}

TEST(DmxDayAlign, ImplausibleAndWarningRules) {
    EXPECT_FALSE(DmxDayAlign::implausibleStart(5000 + H, 5000));
    EXPECT_TRUE(DmxDayAlign::implausibleStart(5001 + H, 5000));
    EXPECT_FALSE(DmxDayAlign::realignIsWarning(1, 600000));
    EXPECT_FALSE(DmxDayAlign::realignIsWarning(-1, DAY + 600000)) << "alive across midnight";
    EXPECT_TRUE(DmxDayAlign::realignIsWarning(-1, 600000)) << "head in day 0: unexplained";
    EXPECT_EQ(DmxDayAlign::realignLine(1, DAY + 605000, 600000),
              "MTC day realign k=1 (engine 87005000 ms, local head 600000 ms, phase=ext)");
}

// The production resolver: MtcReceiver::toLocalDay and its authoritative flag.
TEST(DmxDayAlign, ProductionResolverFollowsTheReceiversFlag) {
    MtcReceiver::resetStaticStateForTesting();
    MtcReceiver::mtcHead.store(600000);
    long start = -7, k = -7;
    EXPECT_FALSE(DmxDayAlign::resolveAbsolute(DAY + 605000, 600000, MtcReceiver::toLocalDay, start, k));
    MtcReceiver::setHeadAuthoritativeForTesting(true);
    ASSERT_TRUE(DmxDayAlign::resolveAbsolute(DAY + 605000, 600000, MtcReceiver::toLocalDay, start, k));
    EXPECT_EQ(k, 1);
    EXPECT_EQ(start, 605000);
    MtcReceiver::resetStaticStateForTesting();
}
