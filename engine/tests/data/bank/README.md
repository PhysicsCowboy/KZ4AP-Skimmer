# Golden data of the bank decoder's tests

The files in this folder are data that the bank decoder's C++ tests read
(`engine/tests/bank/*_test.cpp`, `engine/tests/bank_decoder_test.cpp` and
`bench/tests/bank_json_test.cpp`; the build passes this folder to them as
`KZ4AP_BANK_GOLDEN_DIR`). They stay as data: nothing in the repository
writes them any more.

- **The stage-1 Python prototype's outputs** on fixed, seeded inputs: the
  configuration (`config.json`), each module's results (`filters.json`,
  `noise.json`, `keying.json`, `fit.json`, `fit_cases.json`,
  `periodicity.json`, `periodicity_cases.json`, `selection.json`), the
  channel decoder's full runs (`channel.json`), and the complex64 test
  streams they were computed on (`noise_stream.c64`,
  `channel_stream_*.c64`: interleaved little-endian float32, I then Q, FS).
  The tests reproduce continuous values to relative 10⁻⁹ and discrete
  values exactly (`engine/tests/bank/golden.hpp`). They were written by the
  prototype's `training/kz4ap_proto/golden.py`, run from the repository
  root as

      PYTHONPATH=training python -m kz4ap_proto.golden --out engine/tests/data/bank

  Each file's last change: `filters.json` and `fit.json` at commit
  514659d (where `golden.py` last changed), `channel.json`, `keying.json`,
  `noise.json` and every `.c64` stream but one at 1b42ece,
  `periodicity.json`, `periodicity_cases.json` and `selection.json` at
  070b533, `fit_cases.json` at 14ae268 and `config.json` at a953037.
- **`overlap_stream.c64`** is not the prototype's: 10 s (15 000 samples at
  1500 samples/s, complex64) of a recorded oracle channel of the full
  suite, E-qrm-s3 label 9, as the engine's channelizer gives it, added at
  commit 1b2f4ef for
  `BankDecoder.TheConsumersListIsTheBanksAfterEveryUpdateWhenCharactersOverlapInTime`.

The prototype (`training/kz4ap_proto`: `params.py`, `golden.py` and the
decoder modules `bank.py`, `detect.py`, `noise.py`, `keying.py`, `fit.py`,
`periodicity.py`, `text.py`, `select.py`, `channel.py`, `streams.py` and
`testsignals.py`, with their tests `training/tests/test_golden.py` and
`test_proto_bank.py`, `test_proto_channel.py`, `test_proto_detect.py`,
`test_proto_fit.py`, `test_proto_keying.py`, `test_proto_noise.py`,
`test_proto_periodicity.py`, `test_proto_select.py`,
`test_proto_streams.py` and `test_proto_text.py`) was removed on
2026-10-07, when the bank decoder was frozen as the reference (design spec
section 5.2, the amendments of 2026-10-07). Its last version is at commit
68f834b: read a file with, for example, `git show
68f834b:training/kz4ap_proto/golden.py`, or check out a worktree of
68f834b to run the command above there. Checked on 2026-10-07: that
version (run in a worktree of 4aa920c, whose `training/` is the same)
rewrites all 26 of the prototype's files here byte for byte. The
prototype's modules and tests that the C++ tests' comments name are found
at that commit.
