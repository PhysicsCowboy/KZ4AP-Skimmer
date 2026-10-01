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
    reason: str     # "switch" (branch selection), "rekey" (an over's first marks re-keyed) or "timeout" (re-keyed
                    # because the over's amplitude stayed unknown for rekey_timeout_s)


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
            first = min(c.start_s for c in old) if old else cut  # >= lo: the reach is at most reach_s
            self.corrections.append(Correction(t_s, first, t_s - first, old_text, new_text, reason))

    def text(self) -> str:
        return "".join(c.text for c in self.chars)


# The fresh fit's fitted parameters (T, w, q, T_g): k in the 1/2 k ln n penalty (Branch._fresh_wins).
FIT_PARAMETERS = 4


class Branch:
    """One branch's marks and spaces, duration fit and text (spec 4.3, 4.5, 4.7)."""

    def __init__(self, index: int, length_s: float, n: int, rate_hz: float, cfg, text_model: TextModel):
        self.index, self.length_s, self.rate, self.cfg = index, length_s, rate_hz, cfg
        self.delay_s = (n - 1) / (2.0 * rate_hz)  # a boxcar's group delay (linear phase), s
        self.text_model = text_model
        self.fit = DurationFit(cfg)               # the fit that decodes (its best is `current`)
        self.prev_fit: DurationFit | None = None  # the previous over's, until this over's start is re-keyed
        self.rival: DurationFit | None = None     # after a re-key the previous fit won: this over's fresh fit
        self.over_obs: list[tuple[bool, float, float]] = []  # this over's re-keyed and later (is_mark, d s, var s^2)
        self.current = None                       # the Fit that decodes
        self.chars: list[Char] = []
        self.elements = ""
        self.char_start = 0.0
        self.char_end = 0.0
        self.word_open = False
        self.down_at: float | None = None
        self.up_at: float | None = None           # this over's last key-up (None: none yet)
        self.over_start_n = 0                     # sample index of the latest over start
        self.unknown_since_n = 0                  # sample index where the amplitude became unknown (the stream's start)
        self.timeout_from_n = 0                   # the re-key time-out counts from this sample index
        self.over_pending = False                 # an over started by a silence, not yet confirmed by a re-key
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
        enters the decoding fit, and the rival fresh fit while there is one (see _fresh_wins)."""
        if provisional or not d > 0:
            return
        self.fit.add(is_mark, d, var_t)
        self.current = self.fit.best(*prior)
        if self.rival is None:
            return
        self.rival.add(is_mark, d, var_t)
        self.over_obs.append((is_mark, d, var_t))
        fresh = self.rival.best(*prior)
        if self._fresh_wins(fresh, self.current, self.over_obs):
            self.fit, self.current, self.rival = self.rival, fresh, None
        elif len(self.over_obs) >= self.rival.history.maxlen:
            # By now the previous over's memory weighs lambda^(4 N_mem) = e^-4 = 1.8% of the continued fit's
            # (derived), so the two fits nearly agree: the competition ends and the continued fit stays.
            self.rival = None

    def _fresh_wins(self, fresh, old, obs) -> bool:
        """Whether this over's fresh fit replaces the previous over's continued fit (controller ruling, Task 11
        review). It needs at least fresh_fit_min_obs of this over's re-keyed (and later) marks and spaces, and
        its log-likelihood on them must beat the continued fit's by 1/2 k ln n, nats: k = FIT_PARAMETERS = 4,
        the parameters the fresh fit fits (T, w, q, T_g), n = len(obs). The form is the BIC's (derived, for n
        independent observations); applying it with k counting only the fresh fit's parameters is heuristic.
        Without it the fresh fit, fitted in-sample to 2-3 re-keyed durations, won almost every re-key (the
        previous fit won 8 of 32 in the review's same-speed turnover) and could misread the over's speed."""
        n = len(obs)
        if fresh is None or n < self.cfg.fresh_fit_min_obs:
            return False
        if old is None:
            return True
        gain = n * (observations_loglik(fresh, obs, self.cfg) - observations_loglik(old, obs, self.cfg))
        return gain > 0.5 * FIT_PARAMETERS * math.log(n)

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

    def new_over_due(self, n_now: int, key_down: bool) -> bool:
        """The key has been up for longer than T_new since the last key-up; both times on this branch's time
        base (group delay removed), s."""
        return (not key_down) and self.up_at is not None and self.time(n_now) - self.up_at > self.silence_limit_s()

    def start_over(self, n_now: int, prior, was_unknown: bool) -> None:
        """A possible new over (spec 4.7): a fresh fit, the previous over's kept as the fallback (its best with
        the T_P prior decodes until the re-key). was_unknown: the amplitude was still unknown (an over that was
        never re-keyed starts again), so the re-key and its time-out keep counting from where it became
        unknown."""
        self._finish_char()
        self._word_space()
        if self.prev_fit is None and self.fit.history:
            self.prev_fit = self.fit
        self.fit = DurationFit(self.cfg)
        self.rival = None
        self.over_obs = []
        self.current = self.prev_fit.best(*prior) if self.prev_fit is not None else None
        self.over_start_n = n_now
        if not was_unknown:
            self.unknown_since_n = self.timeout_from_n = n_now
        self.over_pending = True
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
        """Re-keys the stretch from sample n0 with the full LLR (spec 4.7). For each candidate amplitude s^2
        (FS^2), the re-keyed marks and spaces enter a fresh fit and the previous over's fit continued; the fresh
        one is taken only if _fresh_wins, otherwise the continued one decodes and the fresh one stays its rival,
        learning from every later mark and space. Among the amplitudes, the one whose taken fit explains its
        re-keyed marks and spaces best (mean log-likelihood per element, nats) wins, and the stretch's
        characters are decoded again with that fit. With no mark or space at any amplitude, the previous over's
        fit continues. Returns (s^2 FS^2, key state at the end, the stretch's start time s, marks keyed)."""
        from_s = self.time(n0)
        best = None
        for amp2 in amp_candidates:
            key = rekey(P_stretch, sigma2, amp2, self.cfg, a_min)
            var_t = resolution_var_s2(self.length_s, math.sqrt(max(amp2, 0.0) / sigma2), self.rate)
            obs, down_at, up_at = self._observations(key, n0, var_t)
            obs3 = [o[:3] for o in obs]
            fresh = DurationFit(self.cfg)
            for is_mark, d, v in obs3:
                fresh.add(is_mark, d, v)
            fresh_result = fresh.best(*prior)
            if self.prev_fit is None:
                fit, result, rival = fresh, fresh_result, None
            else:
                cont = self.prev_fit.copy()
                for is_mark, d, v in obs3:
                    cont.add(is_mark, d, v)
                cont_result = cont.best(*prior)
                if self._fresh_wins(fresh_result, cont_result, obs3):
                    fit, result, rival = fresh, fresh_result, None
                else:
                    fit, result, rival = cont, cont_result, fresh
            score = observations_loglik(result, obs3, self.cfg)
            if best is None or score > best[0]:
                best = (score, amp2, key, fit, result, rival, obs, down_at, up_at)
        _, amp2, key, fit, result, rival, obs, down_at, up_at = best
        self.fit, self.prev_fit = fit, None
        self.over_obs = [o[:3] for o in obs] if rival is not None else []
        self.rival = rival if rival is not None and len(self.over_obs) < rival.history.maxlen else None
        if result is not None:
            self.current = result
        self._redecode(from_s, obs, self.current)
        marks = sum(1 for o in obs if o[0])
        self.marks_in_over = marks
        self.down_at, self.up_at = down_at, up_at
        return amp2, bool(key[-1]) if len(key) else False, from_s, marks

    def clear_over(self, n0: int) -> float:
        """The re-key time-out with no amplitude to key with (no previous over): nothing in the stretch from
        sample n0 is keyed, so its provisional characters are deleted; the amplitude stays unknown. Returns the
        stretch's start time, s."""
        from_s = self.time(n0)
        self._redecode(from_s, [], None)
        self.marks_in_over = 0
        self.down_at = self.up_at = None
        return from_s

    def _redecode(self, from_s: float, obs, fit) -> None:
        """Drops the characters from from_s (s) on and decodes obs (is_mark, d s, var s^2, start s, end s) with
        fit (none: nothing is decoded)."""
        self.chars = [c for c in self.chars if c.start_s < from_s]
        self.word_open = bool(self.chars) and self.chars[-1].text != " "
        self.elements = ""
        if fit is None:
            return
        for is_mark, d, v, start, end in obs:
            if is_mark:
                self._add_element(start, end, v, fit)
            else:
                self._end_space(d, v, fit)

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
    # (t s, T_P s or NaN, confidence, window s or NaN, per window [(T s or None, score)]); confidence and scores are
    # dimensionless for the combs ("comb", "edge") and in nats for the spectrum fit ("spectrum")
    periodicity: list = field(default_factory=list)
    # s, the selected branch's time base (group delay removed): where an over started, counted once its re-key
    # keyed at least one mark (a silence followed only by noise starts no over)
    over_starts: list = field(default_factory=list)
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
        timeout = int(round(cfg.rekey_timeout_s * rate))
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
                if br.new_over_due(n1, bool(keyer.key[k])):
                    br.start_over(n1, prior, was_unknown=bool(keyer.unknown[k]))
                    keyer.start_over(k)
                elif keyer.unknown[k]:
                    # The re-keyed stretch starts where the amplitude became unknown (at most 20 s back), so it also
                    # covers the provisional characters of overs restarted since then.
                    s = max(br.unknown_since_n, n1 - reach)
                    marks, from_s, reason = None, None, None
                    if br.marks_in_over and keyer.weight[k] >= keyer.rekey_weight:
                        # W_min of keyed time since the over started (keyer: only keyed samples count while unknown)
                        candidates = [float(keyer.amp2[k])]
                        if math.isfinite(keyer.prev_amp2[k]):
                            candidates.append(float(keyer.prev_amp2[k]))
                        amp2, key_now, from_s, marks = br.rekey_over(P[k, s:n1], s, float(sigma2[k]), candidates,
                                                                     float(keyer.a_min[k]), prior)
                        keyer.finish_over_start(k, amp2, key_now)
                        reason = "rekey"
                    elif n1 - br.timeout_from_n >= timeout:
                        # W_min not reached within rekey_timeout_s: re-key what exists with the previous over's
                        # amplitude. If there is none, or it keys nothing, nothing is keyed: the stretch's
                        # provisional characters are deleted and the amplitude stays unknown (the time-out counts
                        # again from now; the stretch still starts where the amplitude became unknown).
                        prev2 = float(keyer.prev_amp2[k])
                        if math.isfinite(prev2) and rekey(P[k, s:n1], float(sigma2[k]), prev2, cfg,
                                                          float(keyer.a_min[k])).any():
                            amp2, key_now, from_s, marks = br.rekey_over(
                                P[k, s:n1], s, float(sigma2[k]), [prev2], float(keyer.a_min[k]), prior)
                            keyer.finish_over_start(k, amp2, key_now)
                        else:
                            from_s = br.clear_over(s)
                            br.timeout_from_n = n1
                            keyer.start_over(k)  # still unknown: W_min of keyed time counts afresh from now
                        reason = "timeout"
                    if reason is not None:
                        if marks and br.over_pending and k == selector.current:
                            result.over_starts.append(br.time(br.over_start_n))
                        if marks is not None:
                            br.over_pending = False
                        if k == selector.current:
                            out.replace_from(from_s, br.chars, t_now, reason)
            instants = sum(1 for _, down in changes[0] if not down)
            if instants:
                views = [BranchView(k, br.length_s, br.current, br.text_logprob(cfg.text_window_chars))
                         for k, br in enumerate(branches)]
                old = selector.current
                new = selector.update(views, instants, t_now, prior[0])
                if new != old:
                    result.switches += 1
                    since = selector.eligible_since[new]  # stream time, s: on the new branch's time base below
                    start = _char_start_at(branches[new].chars,
                                           (since if since is not None else t_now) - branches[new].delay_s)
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
