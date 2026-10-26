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
    EXPECT_TRUE(rep.anyShifted);
    EXPECT_EQ(rep.lastK, 1);
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

// now / +rel never go through the conversion: relative to the play head, whatever its day.
TEST(DmxDayAlign, NowAndRelativeUntouchedPastTwelveHours) {
    EXPECT_EQ(DmxDayAlign::parseMtcTime("now").kind, DmxDayAlign::TimeKind::Now);
    const auto rel = DmxDayAlign::parseMtcTime("+2");
    EXPECT_EQ(rel.kind, DmxDayAlign::TimeKind::Relative);
    EXPECT_EQ(13 * H + rel.ms, 13 * H + 2000);
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
