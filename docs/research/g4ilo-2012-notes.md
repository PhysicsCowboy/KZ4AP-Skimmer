# G4ILO's Blog, "Best Morse Decoder" (2012) — verification notes

## 1. Citation

Julian [callsign G4ILO, blog byline shows as "Unknown"], "Best Morse Decoder,"
*G4ILO's Blog* (Blogger), posted Monday, December 10, 2012.
URL: http://blog.g4ilo.com/2012/12/best-morse-decoder.html
Saved as: `C:\KZ4APSkimmer-papers\G4ILO's Blog_ Best Morse Decoder.pdf` (3 pp.,
printed from the live page on 2026-09-27; page numbers below are PDF pages,
i.e., print pagination of the blog page, not a paginated original document).
The post itself is short (a few paragraphs); most of the document is the
9-comment reader thread plus blog sidebar/navigation chrome.

This is a hobbyist blog post, not a technical paper: no methodology section,
no quantitative metric, no data availability. It is included in the project's
source set as informal, secondhand evidence about CW Skimmer's and MRP40's
real-world behavior, and is treated accordingly (all findings below are
[secondhand]/[opinion], never [fact] in the sense of a documented spec).

## 2. What was compared, how, and what was found

### Decoders discussed
- **MRP40** (Norbert Pieper) and **CW Skimmer** (VE3NEA) — the two decoders
  G4ILO actually ran side by side. [p. 1]
- **fldigi** — G4ILO says he tried it "a few years ago" (i.e., before 2012);
  verdict: "not bad but not the best." Not run side-by-side with the other two
  in this test. [p. 1, p. 2]
- **Digital Master 780 (DM780)**, part of the Ham Radio Deluxe suite — raised
  by a commenter (PA1JIM); G4ILO had not tried it, but says "according to
  Simon Brown the decoding routines in 780 are the same as in fldigi." This is
  a secondhand claim relayed by G4ILO, not something he tested. [p. 2]
- CwGet is **not** mentioned anywhere in this post or its comments.

### Test conditions
- **Off-air, real-time, side-by-side**, not recorded/played-back and not
  synthetic: "I ran both programs simultaneously decoding the same signal."
  [p. 1]
- Signal type: normal on-air amateur QSOs ("After listening to many QSOs...").
  No band, frequency, speed (WPM), or SNR is stated anywhere in the post or
  comments. [not stated]
- No control for fading/QRM/QRN conditions is mentioned; this was ordinary
  listening, not a designed test.

### How results were judged
- Purely by ear/eye: the author compared each program's decoded text against
  what he judged the sender actually sent, watching both programs' text
  output live. No error-rate counting, no logged transcripts, no statistical
  summary — a subjective, informal comparison. [p. 1]

### Findings (G4ILO's own text, p. 1)
- MRP40 "decoded text more accurately."
- Word spacing: "the spacing between words was better [with MRP40] - CW
  Skimmer would often run words together then insert a space in the middle of
  a word."
- Spurious characters: "Skimmer also seemed on occasion to insert a spurious
  E at the beginning of some words or calls when I didn't hear an extra dit."
- Lag: "MRP40's decoder is less laggy than CW Skimmer's - text appeared
  sooner after it was sent."
- AFC (auto frequency control): MRP40's AFC "useful in locking on to
  signals... could track drifting stations and would adjust itself precisely
  to the signal if you didn't click exactly on the trace." By contrast, "CW
  Skimmer seemed more fussy and didn't decode a signal unless you got it
  spot-on."
- G4ILO's own explanation for that fussiness [his inference, not a tested
  claim]: "This is perhaps understandable given that Skimmer is intended to
  be able to distinguish between multiple signals in a pile-up."
- Overall verdict: CW Skimmer is the better program "if you want to decode
  all the calls in a swathe of spectrum" and for logging-program integration
  (new-country/prefix highlighting, worked-before marking) — "what it has
  been designed to do." But "as a morse decoder pure and simple MRP40 is
  still the winner in my book."
- G4ILO does **not** say anything, anywhere in his own text, about weak-signal
  performance or sensitivity for either program. That topic appears only in a
  commenter's rebuttal (see below).

### Notable reader comments [comments — clearly distinguished from the post]
- **Paul Stam PC4T** (first reply, later corrected as a typo): initially
  wrote "I do agree with you," then posted a follow-up "Hi Hi... I meant: I
  do not agree with you. Freudian mistake?" His substantive comment
  disagrees with G4ILO: he tested MRP40 and CW Skimmer side by side and found
  **CW Skimmer** the winner ("It will never let me down"). He cites eHam
  user-review scores: MRP40 3.9/5, CW Skimmer 4.4/5, and quotes a reviewer
  "ON6KE" via eHam calling Skimmer "a great help during contests." His
  specific complaints about MRP40: (1) no simultaneous decoding — Skimmer
  does "100's," MRP40 "just 1"; (2) MRP40's decode quality is inferior to
  Skimmer's; (3) MRP40 requires manually setting a level on the
  "oscillogram"; (4) MRP40's text formatting is dated; (5) MRP40 has
  fixed-size windows. He adds that CW Skimmer is "highly over priced" but he
  doesn't regret buying it. [p. 1–2]
- **Paul Stam PC4T** (second, later comment): elaborates that in his
  opinion CW Skimmer is "especially" better "for weak signals, that's where
  MRP40 fails (in my opinion). But good for a second place." He also states
  DM780's and fldigi's CW decoders are "lousy," calls the human brain/ear the
  best CW decoder, and says he will "never rely only on a CW decoder." **This
  weak-signal claim is Paul Stam PC4T's opinion in a comment, not a finding
  or claim by G4ILO.** [p. 2]
- **PA1JIM ("Jim," posted as Anonymous)**: asks whether G4ILO has tried DM780
  as a CW decoder, since he runs it in the background to catch missed text.
  [p. 2]
- **VE9KK ("Mike")**: had tried MRP40 and found it decoded well; had not yet
  gotten CW Skimmer working with his KX3 at the time of writing, planned to
  compare once he did. (No later follow-up comment from him is in this
  thread.) [p. 2]
- **ag1le ("Mauri AG1LE")** (comment dated March 27, 2013, ~3.5 months after
  the post): asks whether G4ILO has tried the FLDIGI CW decoder, mentions he
  is "currently testing some new algoritms [sic]" for it, and links to his
  own blog post on a "probabilistic neural network classifier" (Feb 2013).
  G4ILO replies (March 28, 2013) that he tried fldigi's CW decoder "a few
  years ago" and would try it again. [p. 2–3] Note: AG1LE (Mauri) is already
  a primary secondary source elsewhere in this project's survey (Bayesian
  Morse decoder work, CER-vs-SNR testing) — this comment is the direct link
  between that body of work and G4ILO's post, but adds no new technical
  content here.

## 3. Verification against the project's current text

Checked `docs\research\decoder-survey.md` and
`docs\research\research_notes\CW decoder algorithms survey\cw_skimmer.md`
and `...\hobbyist_decoders.md` for every G4ILO/MRP40 reference.

- **`cw_skimmer.md`, § 5, line 131**: "Search-result summaries of G4ILO's
  'Best Morse Decoder' post (2012) say it found CW Skimmer excels on weak
  signals, but that MRP40 had better word spacing (Skimmer sometimes ran
  words together or split them) and less lag."
  **CORRECTED.** The word-spacing and lag claims are CONFIRMED as G4ILO's own
  findings [fact, p. 1]. The weak-signal claim is **misattributed**: G4ILO's
  post says nothing about weak-signal performance. That claim — "CW Skimmer
  is better, especially for weak signals, that's where MRP40 fails" — belongs
  to commenter **Paul Stam PC4T**, who is disagreeing with G4ILO, not
  supporting his review. Corrected statement: G4ILO found MRP40 more
  accurate overall, with better word spacing and less lag than CW Skimmer,
  plus useful AFC; commenter Paul Stam PC4T disagreed and separately
  reported (his own unsubstantiated opinion, not tested by G4ILO) that CW
  Skimmer performs better on weak signals. The note can now drop "unverified,
  page did not load" — the page has been read directly.

- **`decoder-survey.md`, line 80**: "G4ILO reportedly found it excellent on
  weak signals but worse than MRP40 at word spacing ([G4ILO blog])
  [secondhand; unverified, page did not load]."
  **CORRECTED**, same issue as above: G4ILO did not say Skimmer is excellent
  on weak signals. Corrected statement: G4ILO reported CW Skimmer ran words
  together and inserted mid-word spaces, was laggier than MRP40, and was
  "fussy" about tuning accuracy compared with MRP40's AFC (word-spacing/lag
  claim CONFIRMED); a commenter, not G4ILO, offered the weak-signal opinion,
  favoring Skimmer there. The "unverified, page did not load" caveat can be
  removed — the page has now been read directly (see notes above).

- **`hobbyist_decoders.md`**: no direct G4ILO citation found (the MRP40/VE9KK
  material there cites a different source, amateurradio.com, not this blog
  post). NOT IN DOCUMENT — no claim to check.

- No other G4ILO/MRP40 statements were found in the survey or cw_skimmer.md
  attributable to this specific post.

## 4. [inference] Effect on ranking or benchmark plan

No change. This is an informal, off-air, un-instrumented, two-program
side-by-side listening comparison with no stated speeds, SNRs, or error
counts, and its most specific claims (word-running-together, mid-word
spurious spaces, lag, fussiness about exact tuning) are anecdotal but
consistent with — and add no new quantitative information beyond — what the
project's decoder survey already documents about CW Skimmer's timing-rigid
segmentation and the RBN's own guidance to senders about needing steady
speed/weighting/spacing (`cw_skimmer.md` §5; `decoder-survey.md` line 80).
The one correction of substance is attribution, not fact: the "excels on weak
signals" claim was never G4ILO's, so it should not be cited as if the
original post-author endorsed CW Skimmer's weak-signal performance. This
does not bear on the ranking rationale (dit-matched filter > CNN+LSTM+CTC >
Bell-style HMM > hybrids), which rests on documented CER-vs-SNR figures
elsewhere, not on this post.
