# Step 1 brief: standard practice for a model-based Morse decoder (blind)

> **Committed 2026-10-14** (owner's approval) from `build/standard-practice-check/`. This is the brief the step-1
> reviewer was given, unchanged except for this note and one path: a folder outside the repository (the session's
> transcripts and memories) is described instead of named, since the project does not commit private paths. A
> first run of step 1 was stopped minutes after it started, because this brief then named example sources and used
> two words that pointed at the design's search method ("pruning", "search errors"). Owner: "any reviewer in this
> role should have to find his own references". The run reported here used this version, which names no source,
> method or earlier decoder; its dispatch message repeated the rules below and also barred the session's temporary
> files. The reviewer's own folder paths below are those of the working copy in `build/`.

You are an independent reviewer for KZ4AP Skimmer, an open-source CW (Morse code) skimmer. The owner, Kenton Brown
(KZ4AP; a physicist who copies Morse by ear up to 50 WPM), and Claude are designing a new decoder for it.

## The owner's request, in his own words

> "I only agree if this is standard practice. I want to be sure we aren't winging it."

> "It does make me wonder if anything else we've put for the earlier topics is also not standard practice, however."

> "As long as we plan to make *measurements* or we have a *real, well-thought-out and checked* theoretical reason in
> the end for one of these heuristics, I'm happy with it. If it's just a dart thrown at a board, we need a better
> way."

> "My real request was to check that everything we were doing in fact was standard."

> "I agree with everything you've said, especially about needing to adapt language recognition and speech
> recognition to Morse code in situations where we cannot find a standard practice."

## How the check works, and why you must not see the design

The check has two steps, done by two different reviewers.

- **Step 1 (you):** from the problem alone, establish what standard practice is for every part of it.
- **Step 2 (another reviewer):** sets the design beside your file and sorts each design choice: standard as the
  literature uses it; standard but adapted; the project's own where a standard exists; the project's own where none
  exists; and standard practices the design does not use at all.

A reviewer who has read a design tends to judge it on its own terms, so you work blind:

- Do not read any file in `C:\KZ4APSkimmer` except this brief and the files you create in
  `C:\KZ4APSkimmer\build\standard-practice-check\`.
- Do not open the project's GitHub repository (PhysicsCowboy/KZ4AP-Skimmer), and do not search the web for the
  project by name.
- Do not read anything under [the local folder of session transcripts and memories] (session transcripts and
  memories).
- Everything you need about the problem is below. If something about the problem is missing, state your assumption
  and go on.

## The problem

### The system

A skimmer receives a slice of an HF amateur band with a software-defined radio, finds every Morse signal in it, and
decodes all of them at once. A detector finds each signal and opens a channel for it. One decoder instance runs per
channel. Decoded text goes to the operator's display and to a separate callsign-search step that issues "spots"
(reports of which station was heard where). The receiver chain up to the channel is built and is not under review.

### The decoder's input

- **One channel per station:** complex baseband at 1500 samples/s (0.67 ms per sample). Linear values are in FS
  (full scale).
- **Channel filter:** linear-phase low-pass, −6 dB relative to the passband at ±150 Hz, flat within 0.015 dB out to
  ±39 Hz, at least 74 dB below the passband beyond ±279 Hz. Its noise bandwidth is 252 Hz. So the channel's noise is
  colored, and its samples are correlated over a few milliseconds.
- **Frequency:** the channel is centered once, on the detector's frequency rounded to a 23.4 Hz grid, and never moves.
  The carrier sits anywhere within ±11.7 Hz of 0 Hz and may drift. The test set drifts up to 40 Hz in total over a
  signal. How much real transmitters drift has not been measured by the project.
- **Start:** the channel opens when the detector has seen a carrier persist. The decoder sees nothing from before
  that. The detector also supplies a rough level: the average power over 1 s, key-up time included.
- **SNR convention:** S₅₀₀ is the key-down carrier power over the noise power in a 500 Hz bandwidth, in dB.

### What arrives on the channel

- **The code:** International Morse: letters, digits, 14 punctuation marks and 12 prosigns. PARIS timing: at w WPM
  the dot lasts T = 1.2/w s and the dash 3T. The gaps are T within a letter, 3T between letters and 7T between
  words.
- **Speeds:** 5–100 WPM.
- **Keying:**
  - Machine keying (keyer or computer): precise timing, possibly weighted. The dash/dot ratio and the mark/space
    balance can differ from nominal; the test set has up to ±0.1 dot of imbalance.
  - Hand keying (straight key, bug, paddle with human spacing): element and gap durations scatter, and the style
    differs by operator (the operator's "fist").
- **Farnsworth spacing:** letters sent at one speed (18–33 WPM in the test set), with stretched gaps between letters
  and words so that the overall speed is lower (5–16 WPM). The gaps are stretched by up to about 16 times.
- **Speed changes:** a station may change speed. The test set steps or ramps the speed by a factor 1.3–2.0, within
  8–80 WPM.
- **Overs:** in a conversation, two or more stations take turns on one frequency. Each turn is an "over". Stations
  usually differ in speed, level and fist. A station answering a call may be 0–10 Hz off the caller's frequency. A
  transmission's first word matters, because it often holds a callsign.
- **Non-Morse content:** silence, tuning carriers (key held down for seconds), random or idle keying.
- **Text:** callsigns, CQ calls, contest exchanges (5NN, serial numbers, zones), DX exchanges, and conversation
  ("ragchews": English plus abbreviations, Q-codes and prosigns).
- **Channel conditions:**
  - white Gaussian noise, with S₅₀₀ from below the level where decoding fails up to 60 dB;
  - Rayleigh fading with Doppler spreads of 0.05–5 Hz, from slow fading over seconds to flutter;
  - impulsive noise (static crashes);
  - other signals in the channel: a neighbor station 0–120 Hz away, from 6 dB weaker to 12 dB stronger than the
    wanted station's key-down power.

### What the decoder must produce

- **Output:** text as it streams, a probability for each character, element timing, speed, and an overall
  confidence. This is the project's common decoder result. The engine's text output allows corrections reaching
  back up to 20 s.
- **Confidence:** it must be trustworthy (calibrated). A hybrid mode hands low-confidence results to a neural
  decoder, so a confidently wrong result is never rechecked.
- **Determinism:** in benchmark mode, the same recording and settings must always give the same output.
- **Metrics:**
  - character error rate: Levenshtein distance divided by the reference length, each prosign counting as one
    symbol, word spaces scored separately;
  - callsign recall and the busted-callsign rate;
  - time to spot after a station starts;
  - CPU time per channel-second, on a desktop and on a Raspberry Pi 5;
  - confidence calibration.

### Constraints and decided scope

- **CPU:** many stations at once on one desktop PC; a busy contest band can hold more than 100 signals. The goal is as
  many stations as the CPU allows. The project's planning estimate is about 2–3 million floating-point operations per
  channel-second (an estimate, not measured).
- **The decoder is model-based and probabilistic:** an explicit statistical model of the received signal, the keying
  and the text, with an inference procedure that gives the most probable text and its probabilities. A trained
  neural decoder (a CNN+LSTM network trained with CTC) is a separate mode, built separately. It is out of scope here
  except as context.
- **No callsign lists in the decoder** (the owner's decision): checking callsigns against lists of known calls
  happens after the decoder, in the callsign-search step. The decoder's text model may know what text looks like,
  but not which calls exist.
- **Testing material:** a synthetic development set with known answers (a jittered grid of the conditions above, with
  development and held-out seeds), and later, real recordings transcribed by the owner.

## Your task

1. **Break the problem into parts.** Cover at least these; rename, split, merge or add as the literature does:
   - P1. Detecting a keyed carrier of unknown amplitude and phase, with a small unknown frequency offset and drift,
     in noise whose samples are correlated, with fading.
   - P2. Unknown and changing timing: speed, Farnsworth spacing, keyer weighting, hand-keying scatter.
   - P3. Estimating the nuisance parameters (amplitude, noise level, frequency offset and drift, speed), separately
     from decoding or jointly with it.
   - P4. The inference framework: the model class, the inference algorithm, what it must keep track of, the
     approximations that fit it into a CPU budget, and making decisions on a stream.
   - P5. Where transmissions start and stop; silence; non-Morse signals; impulsive noise and other signals in the
     channel.
   - P6. The text model: probabilities of letters and words for mixed content, how strongly it is weighted against
     the signal, and how it is estimated.
   - P7. A change of station between overs.
   - P8. The outputs: streaming text with bounded delay and corrections, per-character probabilities, calibrated
     confidence, speed.
   - P9. Running many channels within a CPU budget, and degrading gracefully under overload.
   - P10. How numbers are set and the design is tested: derivation against tuning on held-out data, measuring the
     errors that approximations introduce, calibration, statistical comparison of variants.
2. **For each part, give:**
   - (a) the field or fields that study this sub-problem, and why their practice applies to Morse;
   - (b) the decisions any design must make for this part, and the standard answer to each;
   - (c) the standard method or methods, with sources;
   - (d) the main alternatives, and when each is preferred;
   - (e) what is different about Morse that may make the standard method fit poorly, and how the literature (or you)
     would adapt it;
   - (f) what published Morse decoders did for this part (find them yourself), with neural decoders only for context;
   - (g) where no standard exists: say so plainly, give the nearest standard practice and how it would be adapted
     (the owner expects that speech and language recognition will need adapting);
   - (h) pitfalls: known ways the standard method fails, and the standard checks for them.
3. **Before the parts, a short section:** is there a standard overall architecture for a model-based probabilistic
   decoder of a signal like this? If so, which one, on what evidence? Name the main alternative frameworks.
4. **Classify every statement:**
   - **(S) standard,** with a source: a textbook, a widely cited tutorial or survey, or the documentation of a widely
     used system;
   - **(C) common practice:** used in many published systems but not codified (give examples);
   - **(A) adapted:** there is no standard for this problem, so the nearest standard from another field is used, with
     what changes;
   - **(J) your judgment.**

   Never present (C), (A) or (J) as (S).
5. **Sources: find your own.** This brief names no sources on purpose, so that your choice of references is
   independent of the project's. For each source give the author, year, title, where it was published, and a URL or
   DOI. Say whether you checked the source's text or only its bibliographic record. Prefer primary sources and
   standard textbooks.
6. **Numbers:** where standard practice derives a number (for example a time constant from a bandwidth), give the
   derivation. You may check numbers with numpy through `C:\KZ4APSkimmer\.venv\Scripts\python` (no scipy). Keep any
   scripts in your output folder.

## Output

Write `C:\KZ4APSkimmer\build\standard-practice-check\step-1-standard-practice.md` (git-ignored), with these sections:

1. **Summary table:** one row per decision, giving the part, the decision, the standard answer, the class (S, C, A or
   J) and the source.
2. **The standard overall architecture.**
3. **One section per part,** P1 onward. Number the decisions P1.1, P1.2 and so on, so that step 2 can refer to them.
4. **Prior Morse decoders,** part by part.
5. **Where no standard exists:** the list, with the nearest practice and the adaptation.
6. **References.**

Write so the owner can read it:

- plain terms, with every technical term explained briefly the first time;
- formulas and units where they help;
- every dB names its reference;
- American spelling.

Write incrementally, because the network may drop: create the file early and add to it part by part.

## Rules

- Read-only on the repository: write only in `C:\KZ4APSkimmer\build\standard-practice-check\` (create it if
  needed).
- No git commands.
- Do not dispatch subagents.
- Reply with only these, in under 10 lines:
  - the overall finding on the standard architecture;
  - the count of decisions by class (S, C, A, J);
  - the parts where no standard exists;
  - the output path.
