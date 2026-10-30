# Decision log

The coordinating session's engineering decisions, one line each: what, why, and what it costs if wrong. Newest last.

- 2026-10-30: The newborn's live range is 160 nats. It restores about twice the deepest revival measured on correct stories (67.9 nats under fading), for about 11 ms per channel-second at 500 newborns. If wrong: a one-constant change.
- 2026-10-30: Finish the current per-sample decoder only far enough to score it against the bank on about a twelfth of the test set; the remaining variants and instrumentation are skipped. It shows the best this model can do at any speed, before any work on a fast form. If wrong: about a day of building.
