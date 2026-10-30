#include "display_feed.hpp"

#include <gtest/gtest.h>

#include <variant>

using namespace kz4ap;
using namespace kz4ap::app;

namespace {

Event frame(double time_s) {
    SpectrumFrame f;
    f.time_s = time_s;
    f.power_db.assign(8, -70.0f);
    return Event{std::move(f)};
}

}  // namespace

TEST(DisplayFeed, BuildsRowsAndHandsThemOverOnce) {
    DisplayFeed feed(2);
    feed.on_event(frame(0.1));
    EXPECT_TRUE(feed.drain().rows.empty());
    feed.on_event(frame(0.2));
    const DisplayBatch batch = feed.drain();
    ASSERT_EQ(batch.rows.size(), 1u);
    EXPECT_DOUBLE_EQ(batch.rows[0].time_s, 0.2);
    EXPECT_TRUE(feed.drain().rows.empty());
}

TEST(DisplayFeed, KeepsTrackEventsInOrderAndDropsOnlyOldRows) {
    DisplayFeed feed(1);
    Track track;
    track.id = 3;
    feed.on_event(Event{TrackEvent{TrackEvent::Kind::Born, track}});
    feed.on_event(Event{DecodedTextEvent{3, 0.0, {}, 0.0f, 0.0f}});
    const std::size_t extra = 5;
    for (std::size_t i = 0; i < DisplayFeed::kMaxQueuedRows + extra; ++i) feed.on_event(frame(static_cast<double>(i)));
    const DisplayBatch batch = feed.drain();
    ASSERT_EQ(batch.updates.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<TrackEvent>(batch.updates[0]));
    EXPECT_TRUE(std::holds_alternative<DecodedTextEvent>(batch.updates[1]));
    ASSERT_EQ(batch.rows.size(), DisplayFeed::kMaxQueuedRows);
    EXPECT_EQ(batch.rows_dropped, extra);
    EXPECT_DOUBLE_EQ(batch.rows.front().time_s, static_cast<double>(extra));  // the newest are kept
}
