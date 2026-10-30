# The new decoder in one page (as decided through D23, 2026-10-29)

**The idea.** For one station's channel, keep many competing guesses (*stories*) of what was keyed and when, score
each against the signal sample by sample, and keep the best (Bell 1977). Scores are log-probabilities, in nats. The
details are in the design notes (`2026-10-09-new-decoder-design-notes.md`, last section, Parts 1–17) and the decisions
D1–D23 (`docs/plans/2026-10-20-new-decoder-prototype-1.md`, §3).

**The signal, in order.**
1. **Channel.** The detector finds a station; the channelizer mixes it to 0 Hz, following the detector's frequency,
   and delivers complex samples at 1 500 samples/s.
2. **Front filter.** 25 taps, matched to a 12 ms dot (100 WPM) with 2.5 ms edges; noise bandwidth 74.50 Hz.
3. **Envelope.** Every 4th filter output, Δ = 2.667 ms apart (375 per second), its magnitude e_k (FS): *sample* k.
4. **Noise level.** The hiss estimate gives the noise density N₀ (FS²/Hz); σ_y² = 74.50 Hz × N₀ (FS²) is the noise
   variance of each complex sample behind e_k.

**A story** holds: the text so far; the time of every key-down and key-up; a speed (one of 32, 5.2 to 100 WPM); and a
belief about the carrier's amplitude a (FS), Gaussian with mean μ and variance P.

**At each sample, for each story:**
1. **Branch.** Its mark or gap either continues or ends: a mark as a dot or a dash where Morse allows; a gap as an
   element, letter or word gap, or the end of the over (one station's transmission). Each choice is a new story.
2. **Template.** s_k: the fraction of full carrier the envelope should show now, given the story's edges as smeared
   by the front filter (1 inside a long mark, 0 well inside a gap, between near an edge). v_k: its uncertainty near
   an edge (where within its 2.667 ms the edge fell; other transmitters' rise times, 0.8 to 4.7 ms).
3. **Predict the amplitude.** Since the last update the belief widens as a 2 Hz fading spread allows, and its mean
   relaxes toward 0.886·√Ω̄, where Ω̄ is the story's long-run carrier power (FS²) estimated from its own marks and
   0.886 = √π/2 is the mean amplitude of Rayleigh fading per √power.
4. **Score (D23).** score_k = ln ∫ exp(w·[ln p(e_k | a, V_k) − ln p₀(e_k | σ_y²)]) N(a; μ, P) da, where
   - p is the Rician density of an envelope with carrier a·s_k in complex Gaussian noise of variance V_k, and p₀ that
     of noise alone; so the no-signal story scores 0;
   - V_k = σ_y² + (μ² + P)(f²s_k² + v_k): the noise plus the model's own mismatch (*systematic*), f = 0.02
     (placeholder) and v_k scaled by the story's predicted carrier power;
   - w = 0.1986 (derived) counts each sample as about a fifth of an independent observation, because the front
     filter's noise stays correlated for about 13 ms (5 samples).
5. **Update the amplitude** to its posterior given e_k.
6. **Timing and text costs.** A mark or gap that ends pays the log-probability of its length: log-normal about 1, 3,
   1, 3 and 7 dots (dot, dash, element, letter and word gap), σ = 0.3 in ln(length) (placeholders). A letter pays
   ln 62 = 4.127 nats (62 symbols, equally likely); the gap's kind 0.396, 1.161 or 4.296 nats (letter gap, word gap,
   over's end).

**Newborns.** The silent story can start a new over at any sample (0.05 per second, placeholder). Its level is
unknown over 100 dB (flat in dB from −15 to +85 dB S₅₀₀), so its first mark is scored on a 128-point grid over ln a
instead of the Gaussian, and handed over when the belief is narrow (standard deviation of ln a under 0.1), or
at the latest 15 samples after the mark's key-up, its template's last nonzero sample. At each letter start a *rescue* newborn may restart a story's level and speed, its text
kept (10⁻³, placeholder).

**The search.** Stories in the same state merge, the better kept. Stories more than 40 nats behind the best are
dropped, and beyond 1 700 stories the worst (both placeholders).

**Output.** The best story's text, each letter with its claimed start and end (keyed time plus 0 to 2.667 ms).

**Open** (to the owner one at a time): the newborn under fast fading; the signal's timing evidence weighted about 2.5
times too lightly against the timing costs; f treated as independent from sample to sample; the edge position not
handled jointly across its samples; √Ω̄ used where the expected √Ω is meant.
