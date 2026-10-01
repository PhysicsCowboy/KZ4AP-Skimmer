"""One channel through the filter bank (spec sections 3 and 4): 32 branches, each with its own amplitude,
keying, timing, duration fit and text; the periodicity estimate; branch selection; new overs with
re-keying (spec 4.7); and the channel's text with corrections (spec 4.8)."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass, field

import numpy as np

from .bank import boxcar, branch_lengths_s, branch_samples
from .fit import DurationFit, classify_mark, classify_space, observations_loglik, resolution_var_s2
from .keying import BankKeyer, edges, rekey
from .noise import make_noise
from .periodicity import Periodicity
from .select import BranchView, Selector
from .text import TextModel, decode_pattern


@dataclass
class Char:
    text: str       # a symbol, "*" for a pattern with no code, or " " for a word space
    start_s: float  # stream time, the branch's group delay removed, s
    end_s: float


@dataclass
class Correction:
    t_s: float      # when it was issued, s
    from_s: float   # the replaced text started here, s
    reach_s: float  # t_s - from_s, s (at most the correction reach)
    old: str
    new: str
    reason: str     # "switch" (branch selection) or "rekey" (an over's first marks re-keyed)


class Output:
    """The channel's text (spec 4.8): the selected branch's characters appended as they are decided, and
    corrections that replace everything from a time on, reaching back at most reach_s."""

    def __init__(self, reach_s: float):
        self.reach_s = reach_s
        self.chars: list[Char] = []
        self.corrections: list[Correction] = []

    def append_new(self, chars) -> None:
        """Appends the characters (in time order) that start after the last one published."""
        last = self.chars[-1].start_s if self.chars else -math.inf
        fresh = []
        for c in reversed(chars):
            if c.start_s <= last:
                break
            fresh.append(c)
        self.chars.extend(reversed(fresh))

    def replace_from(self, from_s: float, chars, t_s: float, reason: str) -> None:
        """Replaces the published text from from_s (s) on by chars (another branch's, or the same branch's
        re-decoded), at time t_s (s); nothing that starts more than reach_s before t_s changes.
        Different branches time the same character a few ms apart (each removes its own group delay, but its
        edges cross at different points of its ramps), so the cut is made on overlap, not on start times alone:
        a published character that has not ended by the cut is replaced (one that starts earlier than
        t_s - reach_s is kept), and a new character is taken only if it starts at or after both the cut and the
        end of the last kept character. (Cutting on start times alone kept the old branch's copy of a
        character that began 0.7 ms before the new branch's copy, and published it twice: "CQQ".)"""
        lo = t_s - self.reach_s
        cut = max(from_s, lo)
        keep = [c.start_s < lo or c.end_s < cut for c in self.chars]
        kept = [c for c, k in zip(self.chars, keep) if k]
        old = [c for c, k in zip(self.chars, keep) if not k]
        after = max([cut] + [c.end_s for c in kept])  # > cut only for a kept character that starts before lo
        new = [c for c in chars if c.start_s >= after]
        self.chars = kept + new
        old_text, new_text = "".join(c.text for c in old), "".join(c.text for c in new)
        if old_text != new_text:
            first = min([cut] + [c.start_s for c in old])  # >= lo: the reach is at most reach_s
            self.corrections.append(Correction(t_s, first, t_s - first, old_text, new_text, reason))

    def text(self) -> str:
        return "".join(c.text for c in self.chars)


class Branch:
    """One branch's marks and spaces, duration fit and text (spec 4.3, 4.5, 4.7)."""

    def __init__(self, index: int, length_s: float, n: int, rate_hz: float, cfg, text_model: TextModel):
        self.index, self.length_s, self.rate, self.cfg = index, length_s, rate_hz, cfg
        self.delay_s = (n - 1) / (2.0 * rate_hz)  # a boxcar's group delay (linear phase), s
        self.text_model = text_model
        self.fit = DurationFit(cfg)
        self.prev_fit: DurationFit | None = None  # the previous over's, until this over's start is decided
        self.current = None                       # the Fit that decodes
        self.chars: list[Char] = []
        self.elements = ""
        self.char_start = 0.0
        self.char_end = 0.0
        self.word_open = False
        self.down_at: float | None = None
        self.up_at: float | None = None           # this over's last key-up (None: none yet)
        self.over_start_n = 0
        self.marks_in_over = 0

    def time(self, n: int) -> float:
        return n / self.rate - self.delay_s

    def on_edges(self, changes, a: float, prior, provisional: bool = False) -> None:
        """Key changes (sample index, key down after it) with the branch's amplitude over noise a.
        provisional: keyed by the unknown-amplitude test (keying.BankKeyer.unknown), so lengthened by up to L
        and liable to merge across short spaces until the over's start is re-keyed (keying.edges)."""
        var_t = resolution_var_s2(self.length_s, a, self.rate)
        for n, down in changes:
            t = self.time(n)
            if down:
                if self.up_at is not None:
                    self._observe(False, t - self.up_at, var_t, prior, provisional)
                    self._end_space(t - self.up_at, var_t)
                self.down_at = t
            elif self.down_at is not None:
                self._observe(True, t - self.down_at, var_t, prior, provisional)
                self.marks_in_over += 1
                self._add_element(self.down_at, t, var_t)
                self.up_at, self.down_at = t, None

    def _observe(self, is_mark: bool, d: float, var_t: float, prior, provisional: bool) -> None:
        """A provisional duration enters no fit (controller ruling, Task 11): not the fresh fit, whose speed it
        would bias long by up to L, nor the previous over's, which the re-keying continues with the re-keyed
        durations (adding the provisional ones too would count the same marks twice). Until the re-keying the
        previous over's fit, if any, decodes them (start_over); with none they are not decoded, and the
        re-keying decodes the whole stretch. After the re-keying every duration is keyed by the full LLR and
        enters the fit, which decodes."""
        if provisional:
            return
        self.fit.add(is_mark, d, var_t)
        self.current = self.fit.best(*prior)

    def _add_element(self, start: float, end: float, var_t: float, fit=None) -> None:
        fit = fit or self.current
        if fit is None:  # a provisional mark with no fit to classify it: the re-keying decodes it
            return
        if not self.elements:
            self.char_start = start
        self.elements += "-" if classify_mark(fit, end - start, var_t, self.cfg) else "."
        self.char_end = end

    def _end_space(self, d: float, var_t: float, fit=None) -> None:
        fit = fit or self.current
        if fit is None:
            return
        kind = classify_space(fit, d, var_t, self.cfg)
        if kind != "element":
            self._finish_char()
        if kind == "word":
            self._word_space()

    def _finish_char(self) -> None:
        if self.elements:
            self.chars.append(Char(decode_pattern(self.elements), self.char_start, self.char_end))
            self.elements = ""
            self.word_open = True

    def _word_space(self) -> None:
        if self.word_open:
            self.chars.append(Char(" ", self.char_end, self.char_end))
            self.word_open = False

    def flush(self) -> None:
        self._finish_char()

    def silence_limit_s(self) -> float:
        """T_new = max(new_over_min_s, new_over_gaps x T_g); T_g of the slowest standard speed without a fit."""
        tg = self.current.tg_s if self.current is not None else 1.2 / self.cfg.min_wpm
        return max(self.cfg.new_over_min_s, self.cfg.new_over_gaps * tg)

    def new_over_due(self, t_now: float, key_down: bool) -> bool:
        return (not key_down) and self.up_at is not None and t_now - self.up_at > self.silence_limit_s()

    def start_over(self, n_now: int) -> None:
        """A possible new over (spec 4.7): a fresh fit, the previous over's kept as the fallback."""
        self._finish_char()
        self._word_space()
        if self.prev_fit is None and self.fit.history:
            self.prev_fit = self.fit
        self.fit = DurationFit(self.cfg)
        self.current = self.prev_fit.best() if self.prev_fit is not None else None
        self.over_start_n = n_now
        self.marks_in_over = 0
        self.down_at = self.up_at = None

    def _observations(self, key, n0: int, var_t: float):
        obs, down_at, up_at = [], None, None
        for n, down in edges(np.asarray(key, bool)[None, :], np.zeros(1, bool), n0)[0]:
            t = self.time(n)
            if down:
                if up_at is not None:
                    obs.append((False, t - up_at, var_t, up_at, t))
                down_at = t
            elif down_at is not None:
                obs.append((True, t - down_at, var_t, down_at, t))
                up_at, down_at = t, None
        return obs, down_at, up_at

    def rekey_over(self, P_stretch, n0: int, sigma2: float, amp_candidates, a_min: float, prior):
        """Re-keys the stretch from sample n0 with the full LLR (spec 4.7): each candidate amplitude s^2 with
        each candidate fit (fresh, and the previous over's continued); the combination whose fit explains the
        re-keyed marks and spaces best (mean log-likelihood per element) wins, and the stretch's characters are
        decoded again with it. Returns (s^2, key state at the end, the stretch's start time s)."""
        from_s = self.time(n0)
        best = None
        for amp2 in amp_candidates:
            key = rekey(P_stretch, sigma2, amp2, self.cfg, a_min)
            var_t = resolution_var_s2(self.length_s, math.sqrt(max(amp2, 0.0) / sigma2), self.rate)
            obs, down_at, up_at = self._observations(key, n0, var_t)
            for base in [None] + ([self.prev_fit] if self.prev_fit is not None else []):
                fit = DurationFit(self.cfg) if base is None else base.copy()
                for is_mark, d, v, _, _ in obs:
                    fit.add(is_mark, d, v)
                result = fit.best(*prior)
                score = observations_loglik(result, obs, self.cfg)
                if best is None or score > best[0]:
                    best = (score, amp2, key, fit, result, obs, down_at, up_at)
        _, amp2, key, fit, result, obs, down_at, up_at = best
        self.fit, self.prev_fit = fit, None
        if result is not None:
            self.current = result
        self.chars = [c for c in self.chars if c.start_s < from_s]
        self.word_open = bool(self.chars) and self.chars[-1].text != " "
        self.elements = ""
        self.marks_in_over = sum(1 for o in obs if o[0])
        if result is not None:
            for is_mark, d, v, start, end in obs:
                if is_mark:
                    self._add_element(start, end, v, result)
                else:
                    self._end_space(d, v, result)
        self.down_at, self.up_at = down_at, up_at
        return amp2, bool(key[-1]) if len(key) else False, from_s

    def text_logprob(self, window: int):
        recent = []
        for c in reversed(self.chars):
            if len(recent) >= window:
                break
            if c.text != " ":
                recent.append(c.text)
        return self.text_model.mean_logprob(recent)


@dataclass
class ChannelResult:
    text: str
    chars: list = field(default_factory=list)
    corrections: list = field(default_factory=list)
    selections: list = field(default_factory=list)   # (t s, branch index, its T s or NaN)
    periodicity: list = field(default_factory=list)  # (t s, T_P s or NaN, confidence, window s or NaN, per window)
    over_starts: list = field(default_factory=list)  # s, the selected branch's
    switches: int = 0
    p1: np.ndarray | None = None                     # branch 1's squelched posterior at r (run(keep_p1=True) only)

    def to_json(self) -> dict:
        nan_to_none = lambda x: None if x is None or (isinstance(x, float) and math.isnan(x)) else round(x, 6)
        return {"text": self.text,
                "chars": [[c.text, round(c.start_s, 4), round(c.end_s, 4)] for c in self.chars],
                "corrections": [asdict(c) for c in self.corrections],
                "selections": [[round(t, 4), k, nan_to_none(T)] for t, k, T in self.selections],
                "periodicity": [[round(t, 4), nan_to_none(T), round(c, 4), nan_to_none(w),
                                 [[nan_to_none(pt), round(ps, 4)] for pt, ps in per]]
                                for t, T, c, w, per in self.periodicity],
                "over_starts": [round(t, 4) for t in self.over_starts],
                "switches": self.switches}


def _char_start_at(chars, t: float) -> float:
    """The start of the character that contains t (or the first after it); t if none."""
    for c in chars:
        if c.start_s <= t <= c.end_s or c.start_s > t:
            return c.start_s
    return t


class ChannelDecoder:
    def __init__(self, cfg, rate_hz: float):
        self.cfg = cfg
        self.rate = rate_hz
        self.n = branch_samples(branch_lengths_s(cfg), rate_hz)
        self.lengths = self.n / rate_hz  # realized lengths, s
        self.text_model = TextModel()

    def run(self, u, keep_p1: bool = False) -> ChannelResult:
        """Decodes one channel's baseband stream u (station at 0 Hz, r samples/s). keep_p1: also return branch
        1's squelched posterior, the periodicity estimator's input, for the offline experiments of Task 13."""
        cfg, rate = self.cfg, self.rate
        p1_blocks = [] if keep_p1 else None
        u = np.asarray(u, np.complex128)
        total = len(u)
        P = np.stack([np.abs(boxcar(u, int(k))) ** 2 for k in self.n]).astype(np.float32)
        noise = make_noise(cfg, rate, self.n)
        keyer = BankKeyer(cfg, rate, self.lengths)
        periodicity = Periodicity(cfg, rate)
        selector = Selector(cfg, self.lengths)
        branches = [Branch(k, float(self.lengths[k]), int(self.n[k]), rate, cfg, self.text_model)
                    for k in range(len(self.n))]
        out = Output(cfg.correction_reach_s)
        result = ChannelResult("")
        block = max(1, int(round(cfg.block_s * rate)))
        reach = int(round(cfg.correction_reach_s * rate))
        prior = (None, 0.0)
        for n0 in range(0, total, block):
            n1 = min(n0 + block, total)
            t_now = n1 / rate
            noise.update(u, P, n0, n1)
            sigma2 = noise.sigma2()
            key, p, before, a = keyer.step(P[:, n0:n1], sigma2)
            periodicity.push(p[0])
            if p1_blocks is not None:
                p1_blocks.append(p[0].astype(np.float32))
            t_p, confidence, window, updated = periodicity.update()
            prior = (t_p, 1.0) if t_p is not None else (None, 0.0)  # the prior counts once T_P is confident
            if updated:
                result.periodicity.append((t_now, t_p if t_p is not None else math.nan, confidence,
                                           window if window is not None else math.nan, list(periodicity.per_window)))
            changes = edges(key, before, n0)
            for k, br in enumerate(branches):
                if changes[k]:
                    br.on_edges(changes[k], float(a[k]), prior, provisional=bool(keyer.unknown[k]))
                if br.new_over_due(t_now, bool(keyer.key[k])):
                    keyer.start_over(k)
                    br.start_over(n1)
                    if k == selector.current:
                        result.over_starts.append(t_now)
                elif keyer.unknown[k] and br.marks_in_over and keyer.weight[k] >= keyer.rekey_weight:
                    # W_min of keyed time since the over started (keyer: only keyed samples count while unknown)
                    s = max(br.over_start_n, n1 - reach)
                    candidates = [float(keyer.amp2[k])]
                    if math.isfinite(keyer.prev_amp2[k]):
                        candidates.append(float(keyer.prev_amp2[k]))
                    amp2, key_now, from_s = br.rekey_over(P[k, s:n1], s, float(sigma2[k]), candidates,
                                                          float(keyer.a_min[k]), prior)
                    keyer.finish_over_start(k, amp2, key_now)
                    if k == selector.current:
                        out.replace_from(from_s, br.chars, t_now, "rekey")
            instants = sum(1 for _, down in changes[0] if not down)
            if instants:
                views = [BranchView(k, br.length_s, br.current, br.text_logprob(cfg.text_window_chars))
                         for k, br in enumerate(branches)]
                old = selector.current
                new = selector.update(views, instants, t_now, prior[0])
                if new != old:
                    result.switches += 1
                    since = selector.eligible_since[new]
                    start = _char_start_at(branches[new].chars, since if since is not None else t_now)
                    out.replace_from(start, branches[new].chars, t_now, "switch")
                current = branches[new].current
                result.selections.append((t_now, new, current.t_s if current is not None else math.nan))
            out.append_new(branches[selector.current].chars)
        for br in branches:
            br.flush()
        if branches:
            out.append_new(branches[selector.current].chars)
        result.text = out.text()
        result.chars = out.chars
        result.corrections = out.corrections
        if p1_blocks is not None:
            result.p1 = np.concatenate(p1_blocks) if p1_blocks else np.zeros(0, np.float32)
        return result
