#include "display_feed.hpp"
#include "track_table.hpp"

#include "kz4ap/callsign.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
    ASSERT_EQ(batch.updates.size(), 2u + DisplayFeed::kMaxQueuedRows + extra);  // a clock for each whole second of the frames
    EXPECT_TRUE(std::holds_alternative<TrackEvent>(batch.updates[0]));
    EXPECT_TRUE(std::holds_alternative<DecodedTextEvent>(batch.updates[1]));
    ASSERT_EQ(batch.rows.size(), DisplayFeed::kMaxQueuedRows);
    EXPECT_EQ(batch.rows_dropped, extra);
    EXPECT_DOUBLE_EQ(batch.rows.front().time_s, static_cast<double>(extra));  // the newest are kept
    for (std::size_t i = 2; i < batch.updates.size(); ++i) EXPECT_TRUE(std::holds_alternative<StreamClock>(batch.updates[i]));
}

TEST(DisplayFeed, TheClockComesOncePerWholeSecondAmongTheEvents) {
    DisplayFeed feed(100);
    feed.on_event(frame(0.5));
    feed.on_event(frame(0.9));
    Track track;
    track.id = 3;
    feed.on_event(Event{TrackEvent{TrackEvent::Kind::Born, track}});
    feed.on_event(frame(1.2));
    feed.on_event(frame(1.7));
    feed.on_event(frame(3.1));
    const DisplayBatch batch = feed.drain();
    ASSERT_EQ(batch.updates.size(), 4u);
    EXPECT_DOUBLE_EQ(std::get<StreamClock>(batch.updates[0]).time_s, 0.5);
    EXPECT_TRUE(std::holds_alternative<TrackEvent>(batch.updates[1]));
    EXPECT_DOUBLE_EQ(std::get<StreamClock>(batch.updates[2]).time_s, 1.2);
    EXPECT_DOUBLE_EQ(std::get<StreamClock>(batch.updates[3]).time_s, 3.1);
}

namespace {

// A stream whose list hangs on what the stage reads between two events: W1XYZ is read twice in track 1's text, and a correction takes the
// second reading away 10.4 s after its end, just after the stage has read it, as K6QOP's on real 40 m (review 5). The list must not depend on how the
// display's batches fell.
std::vector<Event> stream_with_a_late_correction() {
    std::vector<Event> events;
    Track track;
    track.id = 1;
    track.freq_hz = 1000.0;
    events.emplace_back(TrackEvent{TrackEvent::Kind::Born, track});
    const auto symbols = [](const std::string& text, double t_s) {
        std::vector<DecodedSymbol> out;
        for (const char c : text) {
            out.push_back({std::string(1, c), 1.0f, t_s, t_s + 0.1});
            t_s += 0.1;
        }
        return out;
    };
    events.emplace_back(DecodedTextEvent{1, 1000.0, symbols("E W1XYZ T E W1XYZ T ", 2.0), 25.0f, 1.0f});
    for (int tenth = 30; tenth <= 141; ++tenth) events.push_back(frame(tenth / 10.0));
    // The second W1XYZ ends at 3.7 s, so it is read at 14 s of stream; the correction comes at 14.1 s.
    TextCorrection correction;
    correction.from_index = 12;
    correction.chars = symbols("W1XAZ T ", 3.2);
    events.emplace_back(DecodedTextEvent{1, 1000.0, {}, 25.0f, 1.0f, {correction}});
    for (int tenth = 142; tenth <= 200; ++tenth) events.push_back(frame(tenth / 10.0));
    return events;
}

std::vector<std::string> list_with_batches_of(std::size_t batch_size) {
    const std::vector<Event> events = stream_with_a_late_correction();
    DisplayFeed feed(1000);
    callsign::CallSearch calls;
    TrackTable table;
    const auto drain = [&] {
        for (const TrackUpdate& update : feed.drain().updates) {
            std::visit([&](const auto& event) {  // as the main window's tick does
                table.apply(event);
                calls.apply(event);
            }, update);
        }
    };
    for (std::size_t i = 0; i < events.size(); ++i) {
        feed.on_event(events[i]);
        if ((i + 1) % batch_size == 0) drain();
    }
    drain();
    calls.finish();
    std::vector<std::string> out;
    for (const callsign::CallRow& row : calls.calls()) out.push_back(row.base);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST(DisplayFeed, TheCallListDoesNotDependOnHowTheBatchesFell) {
    const std::vector<std::string> at_each_event = list_with_batches_of(1);
    EXPECT_EQ(at_each_event, std::vector<std::string>{"W1XYZ"});
    for (const std::size_t batch_size : {2u, 7u, 25u, 1000u}) EXPECT_EQ(list_with_batches_of(batch_size), at_each_event) << batch_size;
}
