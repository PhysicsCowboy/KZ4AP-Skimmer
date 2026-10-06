# KZ4AP Skimmer — project instructions

- **Project documents and their roles:**
  - `docs/design/2026-09-25-kz4ap-skimmer-design.md`: *what* and *why*. The
    authority for decisions; changing it needs the owner's approval.
  - `docs/plans/`: one implementation plan per milestone (*how*), written
    from the approved spec before the milestone starts, approved by the owner
    before any code.
  - `docs/backlog.md`: deferred work in milestone order; items move into plans.
  - `docs/signal-processing.md`: what the code actually does now (rule below).
  - `docs/signal-processing-summary.md`: its short companion, for a human
    reader (rule below).
  - `docs/research/`: evidence. `decoder-survey.md` is the synthesis (glossary,
    ranking, benchmark scenarios); per-source verification notes sit beside
    it; `research_notes/` is the original record, annotated, never rewritten.
- **Branches:** work on feature branches. Merging into `main` and pushing need
  the owner's approval. Never rewrite commits (no `--amend`, rebase, or
  resetting commits); fix mistakes with a new commit.
- **`docs/signal-processing.md` describes the engine's signal processing as it
  actually is.** Any change to signal processing — a parameter value, an
  algorithm, the order of stages, a new stage — must update that document in
  the same commit, including its parameter table and whether each choice is
  derived, measured or heuristic. Treat a stale description as a bug.
- **`docs/signal-processing-summary.md` is its companion, kept up to date in
  the same commit** (owner, 2026-10-05). It abbreviates `signal-processing.md`
  to *how* the system works: each stage in order, what it computes (the
  formulas), its parameter values with units, and whether each is derived,
  measured or heuristic. It leaves out results, measurements, port checks,
  test evidence and history. The default signal path comes first; variants
  that are not the default get a sentence each. It must be readable with
  concentration in about 30 minutes (roughly 6 000 words at most). Any change
  that updates `signal-processing.md` updates the summary too, or states in
  the commit why the summary is unaffected.
- Any agent doing implementation work on this project must be told the two
  rules above.
- **Units:** every displayed, logged or documented quantity carries an
  explicit unit. Every dB value names its reference (dBFS, dB SNR in a stated
  bandwidth, dB relative to the passband, etc.); a bare "dB" is a bug. Linear
  signal values are in FS (amplitude, full scale) and FS² (power) unless
  calibrated. See `docs/signal-processing.md`, section 0.

## Working rules for agents (including subagents)

Work often runs unattended, overnight. A command that waits for the owner's
approval stalls everything behind it for hours, so stay inside these forms.

- **Git:** one git command per call, never chained with other commands (no
  `&&`, `;` or `|` around git, no `cd … &&`, no `git -C`). Commit freely on
  feature branches. Pushing and merging into `main` need the owner's
  approval, each time.
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
- **If a command is refused or would need approval,** stop and report it
  with the exact command; do not retry variants of it.
