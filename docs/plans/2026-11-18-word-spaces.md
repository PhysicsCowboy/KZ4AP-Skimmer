# Working plan: word spaces on real senders (2026-11-18)

**Why.** On the seven spare-radio recordings of 2026-11-17 (192 kHz, 15 m to 80 m, one hour each; not committed), judged
against the Reverse Beacon Network's (RBN's) day file, the offline decoder at 98efb9f reported 159 calls the RBN spotted
within the hour and span, CW Skimmer 2.1 (no call list: `DtaFile` empty, validation Normal) 187. Of the 50 calls CW
Skimmer had and we did not, by where our stage lost them (scratch analysis, not committed):

| Where lost | Calls |
|---|---|
| A candidate below 2 points, or held by a near variant | 23 |
| Read, never reported, pruned | 11 |
| Read only one edit off | 8 |
| Never read | 7 |
| Reported as a variant | 1 |

The readings of those 50 calls were 48% joined across word spaces (0.75 point each) and 22% stripped (0.5), against 13%
and 3% for the 137 calls both reported, which were 74% whole words. So the decoder's word spaces cost most of the gap.

**What the gaps show** (decoded symbol times, in dots of the track's speed):

- EA1EYL (20 m, weak): letter gaps about 5 dots, word gaps 10 to 18: a stretched fist (g about 1.6). At the default
  `gap_stretch_prior` 0.002 gaps of 5.1 to 5.4 dots were read as letter and as word spaces alike; at 0.1 they read
  consistently.
- EK8DX (80 m, 29 dB by CW Skimmer): letter gaps 3.0 to 3.9 dots, word gaps 4.3 to 4.8: compressed word spaces only,
  which no fist of the model fits (its stretch scales letter and word gaps alike). Decoded with no word space at all.
- AG4T (40 m, 31 dB): a 7-dot pause inside the call, twice; joined twice, 1.5 points.
- 102 long tracks of 40 m (biased: they are the decoder's own classifications): word over letter gap, median 2.35,
  10% below 1.87; word gap median 8.1 dots.

**Measured so far** (replays and offline decodes of the seven recordings; window-confirmed / misread / unknown):
`joined_points` 1: 163 / 115 / 136 against 159 / 82 / 102, rejected. A sign-off tail rule: at most +1, rejected (branch
`tail-rule-experiment`). `gap_stretch_prior` 0.02: 163 / 79 / 104; 0.1: 162 / 75 / 108, the decoder 7.3 against 5.2 ms per
channel-second (three decodes at once; contention inflates both). The stage's withdrawal fix (`stage-aging`): 160 / 82 / 102.

**Next.**
1. A variant of the segmental decoder's fist with a word-gap scale of its own (word gaps 7 g w T_s, w below 1 for compressed
   spacing), beside the gap stretch; measured on the seven recordings against the RBN, and on the development set's
   seeds (CER and its fist cells) before any default changes, with its CPU against the bar.
2. `gap_stretch_prior` 0.02 or 0.1 on the development set: it changes the plain decoder's text too, so the test set decides.
3. The 2026-11-18 CWT recordings (judged when the RBN posts the day) as a second, held-out check of whatever wins.
