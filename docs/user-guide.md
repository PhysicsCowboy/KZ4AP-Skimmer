# KZ4AP Skimmer user guide

`kz4ap-skimmer` shows a band as a waterfall and decodes every CW signal on it,
from an I/Q recording or a live SDRplay radio (receive only). Building it is
described in [developing.md](developing.md). On Windows the program is
`build\windows\app\Release\kz4ap-skimmer.exe`; on Linux
`build/linux/app/kz4ap-skimmer`.

    kz4ap-skimmer RECORDING.wav

With no argument it opens idle: press **Open...** for a recording, or type your
radio's serial number in **Radio serial** and press **Start radio**.

## The window

- **Waterfall.** The whole span of the recording or radio. A marker above it
  shows each signal the program is following. **Auto floor** holds the color
  scale's floor 5 dB below the median bin power; **Range** sets the scale's
  span in dB above the floor.
- **Track list** (left table): one row per signal found, with its frequency in
  kHz, speed in WPM, level at birth (dB above the noise floor, in one 23.4 Hz bin) and decoded
  text. Data and speech signals also open rows here, and some stay; the call
  list does not take them.
- **Call list** (right table, skimmer mode only): one row per call read well
  enough to report, with its frequency in kHz, when it was last heard, how
  many times it was read, and how: *signed* (sent after DE, CQ or TEST, or
  before K, KN or AR), *bare* (alone, or sent twice), or *named* (only by
  another station, so the frequency is that station's). Click a heading to
  sort. The status bar's "Calls" counts the rows.
- **Text pane** (under the waterfall): click a signal's marker on the waterfall
  (the nearest within 8 pixels), its row in the track list, or a call in the
  call list (the track nearest its frequency within 30 Hz). The pane shows that
  signal's text as it is decoded, with its frequency in kHz, speed in WPM and
  whether it is still live. It keeps following the station when its track ends
  and a new one starts within 30 Hz.
- **Calls on the waterfall** (skimmer mode): each call in the call list whose own station
  was heard at its frequency (within 300 Hz) in the last 60 s of stream is
  written at the top of the waterfall beside that frequency: white if heard in
  the last 16 s, gray before that, in up to three rows. A call only named by
  others gets no label.
- **Transport:** Pause, Restart, **Speed** (1x real time to Max), and
  **Position**, which seeks in a recording; the decoders start afresh from
  there.

## Skimmer and decoder-only modes

**Mode** selects *Skimmer (calls)*, the default, or *Decoder only*: a plain
CW decoder with the waterfall and the text and no call search, call list or
known-call list (`--decoder-only`).

**List** selects *All calls*, the default, or *Signed calls only*: calls sent
at least once after DE, CQ or TEST, or before K, KN or AR (`--signed-only`).

A call is reported once it has been read often enough: fewer readings when it
is on the known-call list or sent signed (after DE, CQ or TEST), more for
unusual forms. A call appears about 10 s after the reading that qualifies it.
The rules and their values are in [signal-processing.md](signal-processing.md),
section 11.

## Known-call lists

**Known calls** takes an optional list of calls in MASTER.SCP's format: a
text file of one call a line. None ships with the program. The call list works
without one.

- **Open list...** loads your own file (`--known-calls FILE`).
- **Download MASTER.SCP** fetches Super Check Partial's list and uses it. It is
  kept in `%LOCALAPPDATA%\kz4ap-skimmer` on Windows
  (`~/.local/share/kz4ap-skimmer` on Linux) and fetched again only when you
  press the button again.
- **No list** goes without.

A binary list (MASTER.DTA) is refused, as is a file with no call in it.

## Command-line options

`kz4ap-skimmer --help` lists them all. The main ones:

| Option | Meaning |
|---|---|
| `RECORDING.wav` | Open this I/Q recording. |
| `--decoder NAME` | `segmental` (the default), `envelope`, `matched`, `bank` or `viterbi`. |
| `--speed X` | Stream seconds per second of wall clock, or `max`. |
| `--threads N` | Threads for the channels (default: half the machine's). |
| `--decoder-only`, `--signed-only` | As the Mode and List boxes. Not together. |
| `--known-calls FILE` | A known-call list (not with `--decoder-only`). |
| `--calls FILE` | Write the call list as CSV when the stream ends. |
| `--quit-at-end` | Quit when the stream ends. |
| `--center-khz KHZ` | The radio's center frequency in kHz, or a recording's in place of its header. |
| `--mirror` | The recording's spectrum is inverted (an SDR on a transceiver's IF output). |
| `--radio`, `--sdrplay-serial SERIAL` | Start that radio at once. |
| `--rate HZ` | The radio's sample rate: 96000, 192000, 384000 or 768000 Hz. |
| `--gain-reduction DB`, `--lna-state N` | The radio's IF gain reduction (20 to 59 dB) and LNA state (0 to 9, 0 the most gain). |
| `--list-sdrplay` | List the SDRplay radios' serial numbers and models, open none, and quit. |
| `--no-settings`, `--settings FILE` | Use no saved settings, or keep them in FILE. |

## Settings kept between runs

When the program quits it saves, in `%LOCALAPPDATA%\kz4ap-skimmer\settings.ini`
on Windows (`~/.config/kz4ap-skimmer/settings.ini` on Linux; `--help` prints
the path): the Mode, the List filter, the known-call list (your file's path, or
the downloaded copy, read from disk only), the Speed, the radio's serial,
center, rate, IF gain reduction and LNA state, the waterfall's auto floor,
floor and range, the window's size and layout, the tables' columns and sort,
and the last recording folder. A saved list that is gone or refused leaves no
list, and the Known calls line says why. The decoder and the threads are not
saved.

An option on the command line sets its choice for that run only: the saved
value stays unless you change that control during the run. Scripted runs
(`--screenshot`, `--report`, `--decoded`, `--calls`, `--quit-at-end`,
`--quit-at`, `--transport`) and offscreen runs read and save no settings unless
`--settings FILE` is given. Deleting the file restores the defaults.

## Other programs' recordings

The program reads stereo I/Q WAV files of 16-, 24- or 32-bit integers or 32-bit
floats, I left and Q right. The center frequency comes from `--center-khz`,
else the file's `auxi` chunk (SpectraVue, HDSDR, CWSL_File), else the file name
(`7000_7192kHz`, `14091kHz`). Rates of 48 000 × 2^k samples/s (48 to 768 kHz)
are used as they are; any other rate above 48 kHz (HDSDR's 250 kHz, for
example) is resampled exactly to the largest of those below it, flat to ±86 kHz
at 192 kHz, with anything that folds into the band at least 80 dB below the
passband (dB re the passband). One-channel receiver audio is not taken by the
app.

## kz4ap-listen and kz4ap-resample

The app makes no sound. `kz4ap-listen` writes what one frequency of a
recording holds as a mono 16-bit WAV, for checking a decoding by ear: the
station shifted to a 600 Hz tone, through a 500 Hz filter (width at −6 dB re
the passband; flat to ±150 Hz around the tone, at least 80 dB below the
passband beyond ±350 Hz), at 12 000 samples/s, with the clip's peak set to
−1 dBFS. It writes a file and plays nothing. For example, 60 s from 155 s into
a recording, at 7029.855 kHz:

    kz4ap-listen IN.wav OUT.wav --khz 7029.855 --start 155 --seconds 60

`--offset-hz` gives the frequency from the center instead; `--tone-hz`,
`--bandwidth-hz`, `--filter-center-hz` and `--rate 8000` change the tone, the
filter and the rate; `--gain-db` sets a fixed gain (dB re unity); `--track-filter`
uses the decoder's own channel filter (±150 Hz at −6 dB re the passband);
`--mirror` and `--center-khz` are as for the app. `--help` lists them.

`kz4ap-resample IN.wav OUT.wav [--mirror] [--center-khz KHZ] [--gain-db DB]`
writes a 16-bit copy at the rate the decoders use (as above), with an `auxi`
chunk holding the center frequency, for programs that need one.

Both are built beside `kz4ap-bench` (`build\windows\bench\Release\` on Windows,
`build/linux/bench/` on Linux).

## The radio

The radio needs the SDRplay API 3.x installed, the same version the program
was built against. The program builds and runs without it, and says so when
asked for the radio.

The program opens only the radio whose serial number you give, in **Radio
serial** or with `--sdrplay-serial`, so that with two radios on the machine it
never takes the wrong one. With no serial it opens none: **Start radio** then
lists the serial numbers it found, as `kz4ap-skimmer --list-sdrplay` does. The
radio starts only when you press Start radio or give `--radio`. The defaults
are 7031.5 kHz center, 192 kHz rate, 40 dB IF gain reduction and LNA state 0.

To record a band: set Radio serial, Center, Rate, IF gain reduction and LNA
state; press **Start radio**; check the waterfall, with no "ADC OVERLOAD"
beside the buttons (raise the gain reduction or the LNA state if there is);
press **Record** and choose a file; press **Stop recording**. The file is 16-bit
stereo WAV, 0.77 MB/s at 192 kHz, up to the format's 4 GiB limit (93 min at
192 kHz), where recording stops by itself. Put the center frequency in the
file name, which this format does not otherwise hold. Center and rate are
locked while recording, and a pipeline that falls behind ("pipeline lost"
beside the buttons) leaves no gap in the file.

## Known limitations

- The track list holds rows of data and speech signals that read as
  plausible; the call list does not.
- At most 400 tracks at once; at the cap a stronger new signal replaces the
  weakest track.
- A station near a stronger neighbor, or within 30 Hz of a steady carrier, is
  read badly or not at all.
- A tune-up carrier writes T's, and a carrier held for most of 5 s fades from
  the text.
- The radio's DC offset at its center frequency is notched out (about 0.03 Hz
  wide), but the raised noise near the center can open short tracks.
- The app has no audio, no tuning and no receiver-audio input.

The full list, with causes, is in [signal-processing.md](signal-processing.md)
(the appendix's Limitations sections).
