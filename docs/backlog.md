# Backlog

Work deliberately deferred from milestone 1, so it isn't forgotten. Each item
says what's wrong, why it was deferred, and how to measure a fix. The design
spec (`docs/design/`) remains the authority; this is a to-do list.

## Next milestone: decoder robustness

Start by adding benchmark scenarios that expose each problem below, then fix
against the numbers. Several of these were found by review but are invisible
to the current benchmark, which only has clean, well-separated, 10–30 dB
signals.

### Benchmark scenarios to add first

- **Strong signals:** SNR up to 60 dB (the generator stops at 30).
- **Stations that stop and pause:** a transmission, then several seconds of
  silence, then another — as between CQs.
- **Stations present from the first sample** of a recording.
- **Tune-up carriers:** an unkeyed carrier of 0.3–2 s before keying starts.
- **Crowded bands:** make the generator's minimum station spacing a setting
  (today it is fixed at 1 kHz) that can go down to zero, so stations 50–200 Hz
  apart and overlapping stations can be tested.
- **Speed range:** 10–60 WPM, including very different speeds side by side.
- **Scoring of the first word** of each transmission, since that is where
  wrong characters currently concentrate.

### Wrong or missing first characters

A station's channel opens only after the detector has seen it persist (about
0.5 s, plus a 1 s warm-up at the start of a recording), so the decoder starts
mid-transmission, often mid-character, and emits a confident wrong symbol
(e.g. `M6Z` for `W6Z`, `RQ` for `CQ`). These produce busted callsigns.

Fix, in two parts:
1. **Replay:** keep the last ~2 s of signal; when a track is born, feed its
   channel from that buffer first so the decoder hears the transmission from
   its start. Decoding runs ~39× real time, so catching up is instant.
2. **Second pass:** once a station's speed and timing estimates have settled,
   re-decode its first characters. For real-time display, show text as it
   arrives and correct it when the better decode lands.

Must land before callsign matching.

### Speed estimate derailed by a short tune-up carrier

An unkeyed carrier of about 0.5–0.95 s pins the classical decoder's speed
estimate at 5 WPM and garbles what follows (carriers of 1 s or more are
already excluded). Milestone 1's original estimator fails the same way, so it
is a limitation of the cluster-based design, not a regression. Candidate
fixes: estimate speed from the mark-plus-space period, or wait for a minimum
number of marks before trusting an update. Related to the second pass above.

### Stray E's after a station stops

After a station stops, a few `E`s can be decoded from noise before its track
is dropped (6 of 26 cases in review). The decoder's noise test uses wideband
noise, but the engine delivers noise narrowed by the channel filter, whose
envelope fluctuates more. Tune the squelch against channel-filtered noise and
add an engine-level test: signal, then 10 s of noise, then no text after the
last real character. Tune together with the next item.

### Tracks outlive their stations

A track is dropped about 8 s after its station stops even with a 1 s timeout,
because the detector's one-second power average takes that long to decay.
The longer a track lingers, the more noise it can decode.

### Ghost tracks beside very strong signals

Around 60 dB SNR, extra tracks appear a few hundred Hz either side of a
station and decode runs of `E` and `I`. Fix idea: reject a peak that sits
inside a much stronger track's skirt (more than X dB below a track within
±N Hz).

### Choose the FFT bin width and channel filter by measurement

Both are guesses. Bins are ~23 Hz (the FFT size is now chosen from the sample
rate to keep that width), and every channel uses a ±150 Hz filter sized for
fast code. Sweep bin width (e.g. 12, 23, 47 Hz) and channel bandwidth against
the scenarios above and pick values by results.

### Channel filters that adapt to each station's speed

The best decoding bandwidth depends on speed: roughly ±30 Hz is enough at
15 WPM, while 50 WPM needs ±100–150 Hz. Start each channel wide, then narrow
it once that station's speed is estimated; combine with the replay so
buffered audio is re-decoded through the narrower filter. Should help weak,
slow signals and crowded bands most.

## Smaller items worth keeping

- **Engine events:** the "station gone" event carries the track as it was when
  found, so its SNR and last-active time are stale; carry the final values.
  Tracks still alive at the end of a recording get no "gone" event.
- **Timestamps:** document the time bases — detection times lag the true start
  by up to ~43 ms, and decoded symbol times include ~10.7 ms of filter delay.
- **Benchmark matching** of labeled signals to tracks is greedy in file order;
  fine at 1 kHz spacing, but needs a proper assignment once crowded scenarios
  exist.
- **Determinism check** in CI runs the same block size twice; add a bench
  option to vary the block size and compare.
- **Test gaps** noted in review: detector hysteresis band and equal-power peak
  ties; channelizer band-edge and adjacent-channel tests use exact-bin tones;
  a direct test that a track's final text precedes its "gone" event (needs a
  way to inject a test decoder).
- **Input checks:** reject infinite values in decoder settings; generator
  arguments (zero or negative counts and durations).
