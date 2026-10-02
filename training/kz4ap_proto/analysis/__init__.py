"""Analysis scripts behind the stage-1 results record (docs/plans/2026-09-30-milestone-2b-stage-1-results.md).
Observation/analysis only: they read decoded files, result files and reports under build/suite/full3 and print
or write tables; none changes a setting or decides anything. Run each from the repository root:

    PYTHONPATH=training python -m kz4ap_proto.analysis.NAME [ARGS]   (--help prints its usage)

Each module's first docstring line says what it computes and the results section it feeds."""


def wants_help(argv) -> bool:
    """True when the command line asks for usage (-h or --help)."""
    return any(a in ("-h", "--help") for a in argv[1:])
