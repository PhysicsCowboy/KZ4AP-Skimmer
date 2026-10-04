import json

import numpy as np
import pytest

from kz4ap_proto.golden import CHANNEL_STREAM_SAMPLES, GOLDEN, STREAMS, write_all
from kz4ap_proto.params import ProtoConfig


# The zero_pad channel stream starts with 1 s of exact zeros: the "spectrum" noise estimate's ratio k / k[0] is then
# 0 / 0 (noise.py, NoiseEstimator.sigma2), the prototype's documented behavior, which the C++ port reproduces and
# pins (docs/signal-processing.md section 8c; a Plan B item).
@pytest.mark.filterwarnings("ignore:invalid value encountered in divide:RuntimeWarning")
def test_write_all_round_trips(tmp_path):
    paths = write_all(tmp_path)
    assert {p.stem for p in paths if p.suffix == ".json"} == set(GOLDEN)
    assert {p.stem for p in paths if p.suffix == ".c64"} == set(STREAMS)
    channel = json.loads((tmp_path / "channel.json").read_text(encoding="utf-8"))
    noise = json.loads((tmp_path / "noise.json").read_text(encoding="utf-8"))
    for p in paths:
        assert p.parent == tmp_path
        if p.suffix == ".json":
            json.loads(p.read_text(encoding="utf-8"))  # every file reloads
    # every stream is complex64 (8 bytes per sample) of the length its JSON states
    for name, samples in CHANNEL_STREAM_SAMPLES.items():
        s = channel["streams"][name]
        assert s["samples"] == samples
        assert len(np.fromfile(tmp_path / s["file"], "<c8")) == samples
    assert len(np.fromfile(tmp_path / noise["stream_file"], "<c8")) == noise["samples"]


def test_config_round_trips_exactly(tmp_path):
    cfg = ProtoConfig()
    (path,) = write_all(tmp_path, only="^config$")
    loaded = json.loads(path.read_text(encoding="utf-8"))
    assert loaded["x_on_values"] == list(cfg.x_on_values)
    assert loaded["eligibility_tolerance"] == cfg.eligibility_tolerance
    assert loaded["block_s"] == cfg.block_s
