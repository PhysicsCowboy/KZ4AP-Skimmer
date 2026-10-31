# Step 2 brief: is every choice in the new decoder's design standard practice?

> **Committed 2026-10-14** (owner's approval) from `build/standard-practice-check/`. This is the brief the step-2
> reviewer was given, unchanged except for this note and one line: two folders outside the repository (the
> session's transcripts and memories, and its temporary files) are described instead of named, since the project
> does not commit private paths. The dispatch message added two rules: the reviewer could read and run step 1's
> derivation script `numbers.py`, and could not read step 1's downloaded source texts or reference-lookup files.
> The folder paths below are those of the working copy in `build/`.

You are an independent reviewer for KZ4AP Skimmer, an open-source CW (Morse code) skimmer. The owner, Kenton Brown
(KZ4AP; a physicist who copies Morse by ear up to 50 WPM), and Claude are designing a new model-based probabilistic
decoder for it, topic by topic.

## The owner's request, in his own words

> "I only agree if this is standard practice. I want to be sure we aren't winging it."

> "It does make me wonder if anything else we've put for the earlier topics is also not standard practice, however."

> "As long as we plan to make *measurements* or we have a *real, well-thought-out and checked* theoretical reason in
> the end for one of these heuristics, I'm happy with it. If it's just a dart thrown at a board, we need a better
> way."

> "My real request was to check that everything we were doing in fact was standard."

> "I agree with everything you've said, especially about needing to adapt language recognition and speech
> recognition to Morse code in situations where we cannot find a standard practice."

> "I think any reviewer in this role should have to find his own references."

## How the check works

The check has two steps, done by two different reviewers.

- **Step 1 (done):** a reviewer who never saw the design set out, from the problem alone, the standard practice for
  every part of the problem, with its own references. Its brief and its output are:
  - `C:\KZ4APSkimmer\build\standard-practice-check\step-1-brief.md` (the problem as it was given);
  - `C:\KZ4APSkimmer\build\standard-practice-check\step-1-standard-practice.md` (its parts P1, P2, … and numbered
    decisions P1.1, P1.2, …).
- **Step 2 (you):** set the design beside step 1's file and sort every choice in it.

The design is recorded in `C:\KZ4APSkimmer\docs\design\2026-10-09-new-decoder-design-notes.md`, branch
`bell-stage-1`, commit b51eefe: topics 1–6 concluded, topics 7–9 not yet discussed, and a section "After review 1"
listing what is open.

## Your task

1. **Read** step 1's brief and output, then the whole design-notes file.
2. **List every choice the design makes:** each conclusion of topics 1–6, every number or setting, every method, and
   the owner's decisions too (the owner wants his own decisions checked). Give each an ID by topic and item, for
   example "T4.2", with the words it rests on quoted briefly.
3. **Sort each choice into one of four classes:**
   - **(i) standard,** used as the literature uses it;
   - **(ii) standard but adapted:** say exactly what is adapted, and whether the notes label the adaptation as theirs;
   - **(iii) ours where a standard method exists:** name the standard method, say what the literature says is gained
     or lost by each, and whether the notes give a reason for not using it;
   - **(iv) ours where no standard exists:** say whether the notes give a checked derivation or a measurement plan
     with a stated method, as the owner requires; if neither, say so.

   Map each choice to step 1's decision numbers (P…). If a choice has no counterpart in step 1, say so.
4. **List the standard practices the design does not use or address at all:** every decision in step 1 that no
   choice in the notes answers, deferred topics 7–9 included (say which topic would take it, if any).
5. **Check the notes' own citations yourself.** For every source the notes cite, find it and confirm that it says
   what the notes claim. Do not rely on the notes, on step 1, or on any earlier review for this.
6. **Check step 1 where it matters:** if you find step 1 wrong or incomplete on a point that changes a classification,
   say so with your own sources. Step 1 is evidence, not authority.
7. **References:** find your own. Every source you rely on gives the author, year, title, where it was published, and
   a URL or DOI. Say whether you checked the source's text or only its bibliographic record.

## What you may read

Only these files in the repository:

- step 1's brief and output, and the design notes (above);
- for context on the system around the decoder (the receiver chain, and the existing decoders the notes mention,
  such as "the bank" and "Matched"): `C:\KZ4APSkimmer\docs\signal-processing.md` and the project spec
  `C:\KZ4APSkimmer\docs\design\2026-09-25-kz4ap-skimmer-design.md`. The spec's §5.2 describes an earlier, Bell-style
  plan that these notes supersede for this decoder.

In particular, do not read:

- anything in `C:\KZ4APSkimmer\docs\research\`: the project's own evidence notes and references. You find your own.
  That folder also holds an earlier review, "review 1" (`2026-10-14-new-decoder-review-1.md` and the folder beside
  it), which the notes cite. Treat the notes' "review 1" citations as claims to check, not as evidence;
- `C:\KZ4APSkimmer\build\design-review\` (the working copy of review 1);
- anything under [the local folders of session transcripts and memories, and of the session's temporary files].

## Output

Write `C:\KZ4APSkimmer\build\standard-practice-check\step-2-comparison.md` (git-ignored), with these sections:

1. **Summary:**
   - the counts by class;
   - the class (iii) choices, most consequential first;
   - the class (iv) choices that have neither a derivation nor a measurement plan;
   - the most important standard practices not used.
2. **The full table:** one row per choice, giving the ID, the choice, the class, step 1's decision numbers, the
   standard method or alternative, a note, and the sources.
3. **Class (iii) in detail,** one subsection each.
4. **Class (iv) in detail:** derivation or measurement plan present, or missing.
5. **Standard practices not used or not addressed.**
6. **Citation checks:** each source the notes cite, with your verdict.
7. **Where you disagree with step 1.**
8. **References.**

Write so the owner can read it:

- plain terms, with every technical term explained briefly the first time;
- formulas and units where they help;
- every dB names its reference;
- American spelling.

Write incrementally, because the network may drop: create the file early and add to it section by section.

## Rules

- Read-only on the repository: write only in `C:\KZ4APSkimmer\build\standard-practice-check\`.
- No git commands, except read-only ones (`git show`, `git log`), one per call and never chained with other
  commands.
- Do not dispatch subagents.
- Reply with only these, in under 10 lines:
  - the counts by class;
  - the three most consequential class (iii) choices;
  - how many class (iv) choices lack both a derivation and a measurement plan;
  - the output path.
