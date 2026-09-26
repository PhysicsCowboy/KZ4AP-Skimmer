# KZ4AP Skimmer — project instructions

- Design spec: `docs/design/`. Deferred work: `docs/backlog.md`.
- **`docs/signal-processing.md` describes the engine's signal processing as it
  actually is.** Any change to signal processing — a parameter value, an
  algorithm, the order of stages, a new stage — must update that document in
  the same commit, including its parameter table and whether each choice is
  derived, measured or heuristic. Treat a stale description as a bug.
- Any agent doing implementation work on this project must be told the rule
  above.
- **Units:** every displayed, logged or documented quantity carries an
  explicit unit. Every dB value names its reference (dBFS, dB SNR in a stated
  bandwidth, dB relative to the passband, etc.); a bare "dB" is a bug. Linear
  signal values are in FS (amplitude, full scale) and FS² (power) unless
  calibrated. See `docs/signal-processing.md`, section 0.
