from kz4ap_proto.power import disable_power_throttling


def pytest_sessionstart(session):
    # Windows power-throttles a background process to about a quarter of its speed after a few seconds (Task 11p
    # report); the suite opts out once. A no-op elsewhere; changes no result.
    disable_power_throttling()
