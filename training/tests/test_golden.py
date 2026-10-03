import json

from kz4ap_proto.golden import GOLDEN, write_all
from kz4ap_proto.params import ProtoConfig


def test_write_all_round_trips(tmp_path):
    paths = write_all(tmp_path)
    assert {p.stem for p in paths} == set(GOLDEN)
    for p in paths:
        assert p.parent == tmp_path
        json.loads(p.read_text(encoding="utf-8"))  # every file reloads


def test_config_round_trips_exactly(tmp_path):
    cfg = ProtoConfig()
    (path,) = [p for p in write_all(tmp_path) if p.stem == "config"]
    loaded = json.loads(path.read_text(encoding="utf-8"))
    assert loaded["x_on_values"] == list(cfg.x_on_values)
    assert loaded["eligibility_tolerance"] == cfg.eligibility_tolerance
    assert loaded["block_s"] == cfg.block_s
