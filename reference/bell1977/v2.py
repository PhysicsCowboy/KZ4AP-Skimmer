"""Runs Bell's compiled program (bell1977.f) and turns its output into scorer input (Bell stage-1 plan, Task 3).

    python reference/bell1977/v2.py run   --bin BIN --tables TABLES --out DIR [--runs 20] [--jobs 10] SETTINGS...
    python reference/bell1977/v2.py texts --out DIR [--letters 200] SETTINGS... > texts.tsv
    kz4ap-bell-score < texts.tsv > scores.csv
    python reference/bell1977/v2.py table --out DIR --scores scores.csv SETTINGS... > v2-fortran.csv
    python reference/bell1977/v2.py rule  [--reading C] v2-fortran.csv
    python reference/bell1977/v2.py golden --run RUNFOLDER --dest engine/tests/data/bell --name NAME

run    For each settings file and run r = 1 .. --runs: a folder DIR/<name>/r<NN> with SETTINGS (the file's seed field
       plus r), MORSEM (written by TABLES, kz4ap-bell-tables), TEXT (105 lines "0") and the program's output files
       (TRANSL.TXT, EVENTS.TXT, stdout.txt). A run whose DONE file exists is skipped, so an interrupted batch
       resumes. The program is run in the folder; a nonzero exit (for example SIGFPE from bell1977-fpe) is recorded
       in FAILED and reported.
texts  One line per finished run, tab-separated: the key <name>/r<NN>, the reference text and the decoded text
       (below), in the project's conventions (A.11): a prosign is one "<..>" token, a word space one blank. The C++
       scorer kz4ap-bell-score reads these lines. Runs that stopped (FAILED) are listed on stderr.
table  The score table (CSV), one row per run: the cell's conditions, Bell's value, run, seed, zero_sum (1: the run
       stopped on SIGFPE at SAVEP's zero sum, and its counts are empty), guard_steps (the decoder steps at which
       change 11 acted, G records), the scorer's counts and the two rates
       (count over chars, 4 decimals). Settings named kam-<WPM>wpm-<S100>db (run/kam-grid, the S100 grid of
       keyboard Morse) get table "grid" and no Bell value.
rule   Plan section 5.2's rule on the score table, with the Bell-style count (decision D10) and Table XVI's reading
       --reading (decision D9: C). Bell's value b % is x = round(2 b) errors in 200 letters. A cell is consistent if
       P(X <= x) >= 0.05 and P(X >= x) >= 0.05 (for x = 0: P(X = 0) >= 0.05), X beta-binomial over 200 letters with
       the pooled rate and the per-run variance (unbiased) by moments; binomial when that variance is not above
       p(1 - p)/200. Runs that stopped are left out of the cell's statistics and counted.
golden One golden input for the C++ tests from a run folder with TRACE = 2, in the layout of
       engine/tests/data/bell/README.md.

Reference and decoded letters, and the cut (our choices; reference sheet (b), (i), (j) items 14 and 15):
- Sent letters come from KEY's element records (EVENTS.TXT, "E"): the marks between two letter-ending spaces, read
  as International Morse, so each letter is labeled by the code keyed (KEY's column C keys B's code and counts as
  B). A letter followed by a word space or a pause is followed by a word space.
- Decoded letters come from TRANSL's letter records ("L"): IALPH's text of the code, a prosign as one token; NBLANK
  >= 1 gives a word space. They are checked against TRANSL's display text (TRANSL.TXT), token by token.
- TRANSL prints a letter when it decides the first mark of the next letter, at its call c, the decided trellis
  stage, about c x 5 ms. The reference is the first --letters sent letters. Decoded letters are kept while
  5 ms x c <= T_cut = (t_{N+1} + t_{N+2}) / 2 + 14.5 ms, where t_k is the start of letter k's first mark and N =
  --letters: halfway between the times at which letter N and letter N + 1 would be printed, with the receiver
  chain's 14.5 ms envelope delay (reference sheet (c)). Heuristic; the margin is at least 2 dots each side.
"""

import argparse
import concurrent.futures
import math
import os
import re
import subprocess
import sys

MORSE = {
    ".-": "A", "-...": "B", "-.-.": "C", "-..": "D", ".": "E", "..-.": "F", "--.": "G", "....": "H", "..": "I",
    ".---": "J", "-.-": "K", ".-..": "L", "--": "M", "-.": "N", "---": "O", ".--.": "P", "--.-": "Q", ".-.": "R",
    "...": "S", "-": "T", "..-": "U", "...-": "V", ".--": "W", "-..-": "X", "-.--": "Y", "--..": "Z",
    ".----": "1", "..---": "2", "...--": "3", "....-": "4", ".....": "5", "-....": "6", "--...": "7", "---..": "8",
    "----.": "9", "-----": "0",
}

# IALPH(1..70), INITL p. 161 07800-08300: the text TRANSL prints for each code.
IALPH = (list("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890") + [";", ":", "%", "&", "", "", "X", ".", "AS", "SN"]
         + ["", "", "", "", "NR", "NO", "GA", "OK", "AR", "SK", "", "", "", ""]
         + ["IMI", "", "", "", "", "BT", "", "", "", "EEE"])
assert len(IALPH) == 70


def token(code):
    """The project's symbol for IALPH code `code` (1-based): a character, or a prosign as one "<..>" token."""
    text = IALPH[code - 1]
    if not text:
        raise ValueError(f"TRANSL printed code {code}, which has no IALPH text")
    return text if len(text) == 1 else f"<{text}>"


SAMPLE_MS = 0.125      # one 8 kHz sample (TAU, p. 160 00600)
STEP_MS = 5.0          # one decoder step (40 samples, p. 159 01800)
ENVELOPE_DELAY_MS = 14.5  # a mark is seen about 14.5 ms late at its 50 % points (reference sheet (c))


def our_fields(line):
    """The six values of a settings file's fifth line (text after "!" is a comment)."""
    fields = line.split("!")[0].replace(",", " ").split()
    if len(fields) != 6:
        raise ValueError(f"settings line 5 needs 6 values: {line!r}")
    return fields


def read_settings(path):
    lines = [ln for ln in open(path, encoding="ascii").read().splitlines() if ln.strip()]
    if len(lines) != 5:
        raise ValueError(f"{path}: expected 5 lines, got {len(lines)}")
    return lines, int(our_fields(lines[4])[0])


def no_core():
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))  # no core file when bell1977-fpe stops on SIGFPE


def run_one(binary, tables, settings, folder, seed):
    if os.path.exists(os.path.join(folder, "DONE")):
        return folder, "skipped"
    os.makedirs(folder, exist_ok=True)
    lines, _ = read_settings(settings)
    ours = our_fields(lines[4])
    ours[0] = str(seed)
    with open(os.path.join(folder, "SETTINGS"), "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines[:4] + [", ".join(ours)]) + "\n")
    with open(os.path.join(folder, "TEXT"), "w", encoding="ascii", newline="\n") as f:
        f.write(" 0\n" * 105)
    subprocess.run([tables, "morsem", os.path.join(folder, "MORSEM")], check=True)
    with open(os.path.join(folder, "stdout.txt"), "w") as out:
        proc = subprocess.run([binary], cwd=folder, stdout=out, stderr=subprocess.STDOUT, preexec_fn=no_core)
    if proc.returncode != 0:
        with open(os.path.join(folder, "FAILED"), "w") as f:
            f.write(f"exit {proc.returncode}\n")
        return folder, f"FAILED exit {proc.returncode}"
    open(os.path.join(folder, "DONE"), "w").close()
    return folder, "done"


def name_of(settings):
    return os.path.splitext(os.path.basename(settings))[0]


def cmd_run(args):
    binary = os.path.abspath(args.bin)
    tables = os.path.abspath(args.tables)
    tasks = []
    for s in args.settings:
        _, base = read_settings(s)
        for r in range(1, args.runs + 1):
            tasks.append((binary, tables, s, os.path.join(args.out, name_of(s), f"r{r:02d}"), base + r))
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for folder, status in pool.map(lambda t: run_one(*t), tasks):
            if status.startswith("FAILED"):
                failed += 1
            print(folder, status, flush=True)
    print(f"{len(tasks)} runs, {failed} failed", flush=True)
    return 1 if failed else 0


def sent_letters(events_path):
    """[(symbol, first-mark sample, word space follows)] from KEY's E records."""
    letters, pattern, first = [], "", None
    for line in open(events_path, encoding="ascii"):
        if not line.startswith("E "):
            continue
        sample, elem = int(line.split()[1]), int(line.split()[2])
        if elem in (1, 2):
            if not pattern:
                first = sample
            pattern += "." if elem == 1 else "-"
        elif elem in (4, 5, 6):
            if pattern not in MORSE:
                raise ValueError(f"{events_path}: keyed pattern {pattern!r} is not a letter or digit")
            letters.append((MORSE[pattern], first, elem in (5, 6)))
            pattern, first = "", None
    return letters, first  # first: start of the unfinished letter's first mark, if any


def decoded_letters(events_path):
    """[(TRANSL call, symbol, word space follows)] from TRANSL's L records."""
    out = []
    for line in open(events_path, encoding="ascii"):
        if line.startswith("L "):
            _, call, _step, code, nblank = line.split()
            out.append((int(call), token(int(code)), int(nblank) >= 1))
    return out


def check_display(folder, decoded):
    """TRANSL.TXT's non-blank tokens, in order, must be the IALPH texts of the L records."""
    display = open(os.path.join(folder, "TRANSL.TXT"), encoding="ascii").read().split()
    expected = [d[1].strip("<>") for d in decoded]
    if display != expected:
        raise ValueError(f"{folder}: TRANSL.TXT does not match the L records")


def join(symbols_with_spaces):
    text = ""
    for symbol, space in symbols_with_spaces:
        text += symbol + (" " if space else "")
    return text.strip()


def texts_of(folder, n_letters):
    events = os.path.join(folder, "EVENTS.TXT")
    sent, _ = sent_letters(events)
    if len(sent) < n_letters + 2:
        raise ValueError(f"{folder}: {len(sent)} letters sent, {n_letters + 2} needed; lengthen TSTOP")
    t1, t2 = sent[n_letters][1] * SAMPLE_MS, sent[n_letters + 1][1] * SAMPLE_MS
    t_cut = (t1 + t2) / 2 + ENVELOPE_DELAY_MS
    decoded = decoded_letters(events)
    check_display(folder, decoded)
    lines, _ = read_settings(os.path.join(folder, "SETTINGS"))
    ours = our_fields(lines[4])
    t_stop_ms = float(ours[2]) * 1000.0
    if t_stop_ms < t_cut + 199 * STEP_MS + 100.0:
        raise ValueError(f"{folder}: the session ends {t_stop_ms:.0f} ms, too soon after T_cut {t_cut:.0f} ms")
    kept = [(sym, sp) for call, sym, sp in decoded if call * STEP_MS <= t_cut]
    reference = join([(sym, sp) for sym, _, sp in sent[:n_letters]])
    return int(ours[0]), reference, join(kept), len(kept), len(decoded), t_cut


def cmd_texts(args):
    """Writes key, reference, decoded (tab-separated) for every finished run; a run that stopped on SIGFPE is
    listed on stderr and left out (the table step records it)."""
    for s in args.settings:
        name = name_of(s)
        folder_base = os.path.join(args.out, name)
        for r in sorted(d for d in os.listdir(folder_base) if re.fullmatch(r"r\d\d", d)):
            folder = os.path.join(folder_base, r)
            if not os.path.exists(os.path.join(folder, "DONE")):
                if os.path.exists(os.path.join(folder, "FAILED")):
                    print(f"{folder}: FAILED, left out", file=sys.stderr)
                    continue
                raise ValueError(f"{folder}: not done")
            _, ref, dec, _, _, _ = texts_of(folder, args.letters)
            print(f"{name}/{r}\t{ref}\t{dec}")
    return 0


# Plan section 5.1: Bell's letter error, %, of each target cell (Table XV, p. 132; Table XVI, p. 134).
BELL_PCT = {
    ("xv", 50, "kam"): {12: 0, 9: 3, 8: 5, 7: 16, 6: 35},
    ("xv", 20, "kam"): {9: 0, 6: 3, 4: 6, 3: 31},
    ("xvi", 30, "good"): {9: 3, 6: 5, 4: 36},
    ("xvi", 30, "fair"): {9: 5, 6: 7, 4: 42},
    ("xvi", 30, "poor"): {9: 12, 6: 13, 4: 46},
    ("xvi", 20, "good"): {9: 1, 6: 4, 4: 6, 3: 31},
    ("xvi", 20, "fair"): {9: 4, 6: 6, 4: 8, 3: 34},
    ("xvi", 20, "poor"): {9: 11, 6: 13, 4: 14, 3: 38},
}


def parse_name(name):
    """xv-50wpm-kam-12db, xvi-20wpm-fair-6db-c or (the S100 grid, run/kam-grid) kam-20wpm-5db -> (table, WPM,
    sender, S100 dB, reading); the grid's table is "grid"."""
    m = re.fullmatch(r"(xvi?)-(\d+)wpm-(kam|good|fair|poor)-(\d+)db(?:-([abc]))?", name)
    if m:
        return m.group(1), int(m.group(2)), m.group(3), int(m.group(4)), (m.group(5) or "-").upper()
    m = re.fullmatch(r"kam-(\d+)wpm-(\d+)db", name)
    if m:
        return "grid", int(m.group(1)), "kam", int(m.group(2)), "-"
    raise ValueError(f"not a target cell or grid point: {name}")


TABLE_COLUMNS = ["table", "speed_wpm", "sender", "reading", "s100_db", "bell_pct", "run", "seed", "zero_sum",
                 "guard_steps", "chars", "bell_count", "char_edits", "spaces", "space_edits", "bell_rate", "char_cer"]


def cmd_table(args):
    """The score table: one row per run, from the run folders and kz4ap-bell-score's CSV."""
    import csv
    scores = {row["key"]: row for row in csv.DictReader(open(args.scores, encoding="utf-8-sig"))}
    out = csv.writer(sys.stdout, lineterminator="\n")
    out.writerow(TABLE_COLUMNS)
    for s in args.settings:
        name = name_of(s)
        table, wpm, sender, s100, reading = parse_name(name)
        bell = BELL_PCT[(table, wpm, sender)][s100] if table != "grid" else ""
        folder_base = os.path.join(args.out, name)
        for r in sorted(d for d in os.listdir(folder_base) if re.fullmatch(r"r\d\d", d)):
            folder = os.path.join(folder_base, r)
            _, seed = read_settings(os.path.join(folder, "SETTINGS"))
            row = [table.upper(), f"{wpm:.2f}", sender, reading, f"{s100:.2f}", bell, int(r[1:]), seed]
            events = os.path.join(folder, "EVENTS.TXT")
            guard = sum(1 for ln in open(events, encoding="ascii") if ln.startswith("G ")) if os.path.exists(events) else 0
            if os.path.exists(os.path.join(folder, "FAILED")):
                out.writerow(row + [1, guard, "", "", "", "", "", "", ""])
                continue
            sc = scores[f"{name}/{r}"]
            chars = int(sc["chars"])
            out.writerow(row + [0, guard, chars, sc["bell_count"], sc["char_edits"], sc["spaces"], sc["space_edits"],
                                f"{int(sc['bell_count']) / chars:.4f}", f"{int(sc['char_edits']) / chars:.4f}"])
    return 0


def betabinom_pmf(n, p, rho):
    """Beta-binomial pmf over 0..n with mean n*p and intra-class correlation rho (binomial when rho == 0)."""
    if p <= 0.0:
        return [1.0] + [0.0] * n
    if p >= 1.0:
        return [0.0] * n + [1.0]
    logc = [math.lgamma(n + 1) - math.lgamma(k + 1) - math.lgamma(n - k + 1) for k in range(n + 1)]
    if rho <= 0.0:
        return [math.exp(logc[k] + k * math.log(p) + (n - k) * math.log1p(-p)) for k in range(n + 1)]
    if rho >= 1.0:  # the limit of complete overdispersion: all n errors or none
        return [1.0 - p] + [0.0] * (n - 1) + [p]
    ab = 1.0 / rho - 1.0
    a, b = p * ab, (1.0 - p) * ab

    def lbeta(x, y):
        return math.lgamma(x) + math.lgamma(y) - math.lgamma(x + y)

    return [math.exp(logc[k] + lbeta(k + a, n - k + b) - lbeta(a, b)) for k in range(n + 1)]


def cell_stats(rows, n_letters):
    """Pooled rate, per-run variance (unbiased) and the fitted beta-binomial's correlation for one cell (plan 5.2):
    Var(rate) = p(1 - p)/m * (1 + (m - 1) rho) for runs of m letters, solved for rho."""
    counts = [int(r["bell_count"]) for r in rows]
    chars = [int(r["chars"]) for r in rows]
    rates = [c / m for c, m in zip(counts, chars)]
    p = sum(counts) / sum(chars)
    k = len(rates)
    mean = sum(rates) / k
    var = sum((x - mean) ** 2 for x in rates) / (k - 1)
    m = n_letters
    binom_var = p * (1 - p) / m
    rho = 0.0 if binom_var <= 0 or var <= binom_var else min(1.0, (var / binom_var - 1.0) / (m - 1))
    return p, var, rho


def cmd_rule(args):
    """Plan section 5.2's rule on the score table; one line per cell and the verdict."""
    import csv
    rows = [r for r in csv.DictReader(open(args.table, encoding="utf-8")) if r["table"] in ("XV", "XVI")]
    reading = args.reading.upper()
    cells = {}
    for r in rows:
        if r["table"] == "XVI" and r["reading"] != reading:
            continue
        cells.setdefault((r["table"], r["speed_wpm"], r["sender"], r["s100_db"]), []).append(r)
    n = args.letters
    inconsistent, above, below, zero_sum_cells = 0, 0, 0, 0
    print("table speed_wpm sender s100_db | bell_pct x | runs zero_sum | p_hat_pct sd_rate_pct rho | "
          "P(X<=x) P(X>=x) | consistent")
    for key in sorted(cells, key=lambda k: (k[0], -float(k[1]), k[2], -float(k[3]))):
        rs = cells[key]
        bell = int(rs[0]["bell_pct"])
        x = round(bell * n / 100)
        finished = [r for r in rs if r["zero_sum"] == "0"]
        zs = len(rs) - len(finished)
        zero_sum_cells += 1 if zs else 0
        head = f"{key[0]} {key[1]} {key[2]} {key[3]} | {bell} {x} | {len(rs)} {zs}"
        if len(finished) < 2:
            print(f"{head} | fewer than 2 finished runs | NO")
            inconsistent += 1
            continue
        p, var, rho = cell_stats(finished, n)
        pmf = betabinom_pmf(n, min(p, 1.0), rho)
        lo, hi = sum(pmf[: x + 1]), sum(pmf[x:])
        consistent = (pmf[0] >= 0.05) if x == 0 else (lo >= 0.05 and hi >= 0.05)
        inconsistent += 0 if consistent else 1
        if not (bell == 0 and p == 0):
            above += 1 if bell / 100 > p else 0
            below += 1 if bell / 100 < p else 0
        print(f"{head} | {100 * p:.2f} {100 * math.sqrt(var):.2f} {rho:.4f} | {lo:.4f} {hi:.4f} | "
              f"{'yes' if consistent else 'NO'}")
    k = above + below
    p_sign = min(1.0, 2 * sum(math.comb(k, i) for i in range(min(above, below) + 1)) / 2 ** k) if k else 1.0
    print(f"cells {len(cells)}; inconsistent {inconsistent} (criterion (i): at most 5); sign test: Bell above "
          f"{above}, below {below}, two-sided p = {p_sign:.3g} (criterion (ii): p >= 0.05); cells with a run that "
          f"stopped at SAVEP's zero sum: {zero_sum_cells}")
    print("PASS" if (len(cells) == 30 and inconsistent <= 5 and p_sign >= 0.05) else "FAIL")
    return 0


def cmd_golden(args):
    """Golden data for the C++ tests from a run folder with TRACE = 2 (layout: engine/tests/data/bell/README.md)."""
    import shutil
    import struct
    src, dst, name = args.run, args.dest, args.name
    os.makedirs(dst, exist_ok=True)
    raw = open(os.path.join(src, "SIGNAL.BIN"), "rb").read()
    n = len(raw) // 24
    signal = struct.unpack(f"<{3 * n}d", raw)
    with open(os.path.join(dst, f"{name}.signal.f64"), "wb") as f:
        f.write(struct.pack(f"<{n}d", *signal[0::3]))
    raw = open(os.path.join(src, "STEPS.BIN"), "rb").read()
    values = struct.unpack(f"<{len(raw) // 8}d", raw)
    steps, paths, i = [], [], 0
    while i < len(values):
        head = values[i:i + 8]
        isave = int(head[3])
        steps.append(head)
        for slot in range(isave):
            rec = values[i + 8 + 7 * slot:i + 15 + 7 * slot]
            paths.append((head[0], float(slot + 1)) + tuple(rec))
        i += 8 + 7 * isave
    with open(os.path.join(dst, f"{name}.steps.f64"), "wb") as f:
        for h in steps:
            f.write(struct.pack("<8d", *h))
    with open(os.path.join(dst, f"{name}.paths.f64"), "wb") as f:
        for p in paths:
            f.write(struct.pack("<9d", *p))
    for src_name, suffix in (("EVENTS.TXT", "events.txt"), ("TRANSL.TXT", "transl.txt"), ("SETTINGS", "settings")):
        shutil.copyfile(os.path.join(src, src_name), os.path.join(dst, f"{name}.{suffix}"))
    print(f"{name}: {n} samples, {len(steps)} steps, {len(paths)} path records, max ISAVE "
          f"{max(int(h[3]) for h in steps)}")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--bin", required=True)
    r.add_argument("--tables", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--runs", type=int, default=20)
    r.add_argument("--jobs", type=int, default=10)
    r.add_argument("settings", nargs="+")
    t = sub.add_parser("texts")
    t.add_argument("--out", required=True)
    t.add_argument("--letters", type=int, default=200)
    t.add_argument("settings", nargs="+")
    tb = sub.add_parser("table")
    tb.add_argument("--out", required=True)
    tb.add_argument("--scores", required=True, help="kz4ap-bell-score's CSV")
    tb.add_argument("settings", nargs="+")
    ru = sub.add_parser("rule")
    ru.add_argument("--reading", default="C", help="Table XVI's reading the rule uses (decision D9: C)")
    ru.add_argument("--letters", type=int, default=200)
    ru.add_argument("table")
    g = sub.add_parser("golden")
    g.add_argument("--run", required=True, help="a run folder with TRACE = 2")
    g.add_argument("--dest", required=True, help="engine/tests/data/bell")
    g.add_argument("--name", required=True)
    args = p.parse_args()
    return {"run": cmd_run, "texts": cmd_texts, "table": cmd_table, "rule": cmd_rule,
            "golden": cmd_golden}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
