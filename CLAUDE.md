# KZ4AP Skimmer — project instructions

- **Project documents and their roles:**
  - `docs/design/2026-09-25-kz4ap-skimmer-design.md`: the project's goals and
    its bar. Changing those needs the owner's approval. How the decoder works
    belongs in `docs/signal-processing.md`, not here (owner, 2026-10-30).
  - `docs/plans/`: results records for checkpoints, and a working plan of one
    page at most where one helps. Plans need no approval (owner, 2026-10-30).
    The new decoder's older plans and design notes stay as history and are
    not maintained.
  - `docs/backlog.md`: deferred work in milestone order.
  - `docs/signal-processing.md`: what the code actually does now (rule below).
  - `docs/research/`: evidence. `decoder-survey.md` is the synthesis (glossary,
    ranking, benchmark scenarios); per-source verification notes sit beside
    it; `research_notes/` is the original record, annotated, never rewritten.
- **How we work (owner, 2026-10-30):**
  - **Decisions:** the coordinating session decides all engineering and keeps a
    one-line log in `docs/decision-log.md`: what, why, and what it costs if
    wrong. The owner never has to read it. Only these go to the owner: pushes,
    any change to the goals or the bar, checkpoints, and what only he can do
    (recordings, for example).
  - **Checkpoints:** one page each: the scores, what changed, how the decoder
    works now, what comes next. Between checkpoints, report only when stuck.
    The owner can ask about anything at any time, and the answer defines its
    terms and gives the formulas.
  - **Choosing:** standard practice is the starting point, and the test set
    decides. A heuristic that wins stays, labeled as a heuristic.
  - **CPU:** stated in ms per channel-second against the bar: at most 40 ms
    required, about 8 ms (500 tracks on half the desktop's cores) the goal.
  - **Measuring:** slices of the test set while iterating, the whole set at
    checkpoints.
  - **Review:** tests always; one independent review before a checkpoint reaches
    the owner or anything merges into `main`.
  - **Subagent effort:** the coordinating session sets each subagent's effort
    itself (owner, 2026-10-30): `high` for building and experiments, `low` or
    `medium` for mechanical work, `max` for stubborn bugs and checkpoint
    reviews.
  - **Memory notes:** the coordinating session may record memories about this
    project without asking and lists them at the next checkpoint (an exception
    to the owner's user-level rule). They go only in this project's memory
    folder, never at user level, and each states the situation it came from and
    what it applies to, so that it is not read more generally than meant.
- **Branches:** work on feature branches. The coordinating session may merge
  into `main` itself, after the review above. Pushing needs the owner's
  approval, each time. Never rewrite commits (no `--amend`, rebase, or
  resetting commits); fix mistakes with a new commit.
- **Languages** (owner, 2026-10-07 and 2026-10-20): new code is C++. Anything
  that processes signals (decoders, detectors, prototypes, experiments over
  recordings) is written in C++, in the engine behind the bench: no Python
  prototypes and no ports. Python stays for the existing test-signal
  generator (`training/kz4ap_synth`), the analysis of results
  (`training/kz4ap_proto`: score tables, fits, figures) and their tests,
  which may be extended in Python, and for scratch calculations kept out of
  the repository. Any other new Python needs the owner's approval.
- **`docs/signal-processing.md` describes the engine's signal processing as it
  actually is** (one document; owner, 2026-10-05). Its **body** is readable
  with concentration in about 30 minutes (6 000 words at most). The body
  covers in full the processing chains under development or discussion: how
  each works, each stage in order with its formulas, every value with its
  unit and whether it is derived, measured or heuristic, each variant that is
  not the default in a sentence. Every other chain gets a short summary
  pointing to its appendix section (owner, 2026-10-08). Its
  **appendix**, organized by the body's sections, holds the derivations, the
  provenance of every measured or heuristic value with its validity
  conditions, the known limitations and defects, the full parameter table and
  the definitions used in tests and the benchmark. Results and evidence
  (measurements, port checks, test evidence, history) live in the results
  records under `docs/plans/`, not in this document. It is brought up to date
  at each checkpoint and before any merge into `main`, and may lag in between
  (owner, 2026-10-30): the body and, where a derivation, provenance or
  limitation changed, the appendix (including the parameter table). A stale
  description at a checkpoint or on `main` is a bug.
- **Test sets and parameter sweeps use a jittered grid** (owner,
  2026-10-06): cells over each condition's range, each value drawn at random
  within its cell; see `docs/plans/2026-10-06-development-set-redesign.md`.
- **Results data and figures** (owner, 2026-10-07; results records only for
  checkpoints, 2026-10-30, so exploratory runs stay in `build/`):
  - Commit the **per-signal score table** (CSV: one row per signal with its
    conditions, cell, CER and character count) of every run a results record
    reports, so every number in the record can be checked and every fit and
    figure redrawn in minutes on any machine. Records cite the commit and the
    score file each number comes from.
  - Score tables are **rounded** to meaningful precision (speed 0.01 WPM,
    S₅₀₀ and E/N₀ 0.01 dB; counts are integers), and a record's numbers are
    computed **from the committed, rounded table**, so the table reproduces
    them exactly. Exact drawn values, if ever needed, come back from the seed
    by rerunning the generator's label step (seconds, no audio).
  - Commit only the **few figures a results record shows**: PNGs embedded in
    the record, at most about three per results section, the ones a conclusion
    rests on, and small enough that the repository does not balloon: about
    1 MB in all per checkpoint at most (owner's concern, 2026-10-30; the limit
    is a heuristic). No approval is needed.
  - Do not commit recordings, decoded files, analysis files (fits,
    bootstraps) or any other figure: they stay in `build/` (git-ignored) and
    are rebuilt from the committed scores (fits and figures, minutes) or from
    the seeds (everything, hours); other figures are sent to the owner
    directly.
- **Units:** every displayed, logged or documented quantity carries an
  explicit unit. Every dB value names its reference (dBFS, dB SNR in a stated
  bandwidth, dB relative to the passband, etc.); a bare "dB" is a bug. Linear
  signal values are in FS (amplitude, full scale) and FS² (power) unless
  calibrated. See `docs/signal-processing.md`, section 0.

## Working rules for agents (including subagents)

Work often runs unattended, overnight. A command that waits for the owner's
approval stalls everything behind it for hours, so stay inside these forms.

- **Git:** commit freely on feature branches. Pushing needs the owner's
  approval, each time. Merging into `main` is the coordinating session's job.
- **Never discard work:** no `git checkout -- <path>`, `git restore <path>`,
  `git reset --hard` or `git clean`. To set changes aside, use `git stash`;
  to experiment without touching the working tree, use a separate worktree
  (`git worktree add`) or a copy under the session's temporary folder, not
  `/tmp`.
- **Reviewers are read-only:** never edit, restore or discard files in the
  repository, even temporarily. Experiment in a worktree or a copy.
- **Deleting:** delete only files you created in the current task. Ask
  before deleting anything else that git does not track, in particular under
  `build/` (locally or on a remote build machine) and `.superpowers/`: they
  hold data that took hours of computation and records that exist nowhere
  else.
- **External hardware** (owner, 2026-11-04): "Nothing in this project should be touching
  external hardware without asking me." That means radios (the SDRplay, the K3), any
  program that controls them (CW Skimmer, OmniRig and the like), COM and USB devices,
  sound devices, and any other program already running that the agent did not start.
  Ask the owner first, each time, through the coordinating session.
- **If a command is refused or would need approval,** stop and report it
  with the exact command; do not retry variants of it.
