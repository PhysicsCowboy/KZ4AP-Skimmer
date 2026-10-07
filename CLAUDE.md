# KZ4AP Skimmer — project instructions

- **Project documents and their roles:**
  - `docs/design/2026-09-25-kz4ap-skimmer-design.md`: *what* and *why*. The
    authority for decisions; changing it needs the owner's approval.
  - `docs/plans/`: one implementation plan per milestone (*how*), written
    from the approved spec before the milestone starts, approved by the owner
    before any code.
  - `docs/backlog.md`: deferred work in milestone order; items move into plans.
  - `docs/signal-processing.md`: what the code actually does now (rule below).
  - `docs/research/`: evidence. `decoder-survey.md` is the synthesis (glossary,
    ranking, benchmark scenarios); per-source verification notes sit beside
    it; `research_notes/` is the original record, annotated, never rewritten.
- **Branches:** work on feature branches. Merging into `main` and pushing need
  the owner's approval. Never rewrite commits (no `--amend`, rebase, or
  resetting commits); fix mistakes with a new commit.
- **`docs/signal-processing.md` describes the engine's signal processing as it
  actually is** (one document; owner, 2026-10-05). Its **body** is readable
  with concentration in about 30 minutes (6 000 words at most): how the
  system works, each stage in order with its formulas, every value with its
  unit and whether it is derived, measured or heuristic; the default signal
  path first, each variant that is not the default in a sentence. Its
  **appendix**, organized by the body's sections, holds the derivations, the
  provenance of every measured or heuristic value with its validity
  conditions, the known limitations and defects, the full parameter table and
  the definitions used in tests and the benchmark. Results and evidence
  (measurements, port checks, test evidence, history) live in the results
  records under `docs/plans/`, not in this document. Any change to signal
  processing — a parameter value, an algorithm, the order of stages, a new
  stage — updates the body and, where a derivation, provenance or limitation
  changes, the appendix (including the parameter table), in the same commit.
  Treat a stale description as a bug.
- Any agent doing implementation work on this project must be told the rule
  above.
- **Figures** (owner, 2026-10-07): commit the analysis files a figure is
  drawn from (small JSON) and only the few figures a results record actually
  shows, as PNG. All other figures are written to `build/` (git-ignored) and
  redrawn from the committed analysis files when needed (`experiments
  figures`); send them to the owner directly rather than committing them.
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
