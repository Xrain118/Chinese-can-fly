import unittest

from ugv_bridge.serial_smoke_test import SmokeTestError, wait_for_required_frames


class FakeConnection:
    def __init__(self, lines):
        self._lines = list(lines)

    def readline(self):
        return self._lines.pop(0) if self._lines else b""


class StepClock:
    def __init__(self, step=0.1):
        self._now = -step
        self._step = step

    def __call__(self):
        self._now += self._step
        return self._now


class SerialSmokeTest(unittest.TestCase):
    def test_accepts_state_and_config_after_unrelated_frames(self):
        connection = FakeConnection(
            [b"T R=0\r\n", b"S R=0,M=0\r\n", b"CFG EC=0\r\n"]
        )
        lines = wait_for_required_frames(connection, 1.0, StepClock())
        self.assertEqual(lines[-2:], ["S R=0,M=0", "CFG EC=0"])

    def test_timeout_reports_both_missing_groups(self):
        with self.assertRaisesRegex(SmokeTestError, r"config, state"):
            wait_for_required_frames(FakeConnection([]), 0.3, StepClock())

    def test_non_ascii_data_reports_likely_link_mismatch(self):
        with self.assertRaisesRegex(SmokeTestError, r"non-ASCII"):
            wait_for_required_frames(FakeConnection([b"\xff\xfe\r\n"]), 1.0, StepClock())

    def test_missing_config_frame_is_rejected(self):
        with self.assertRaisesRegex(SmokeTestError, r"config"):
            wait_for_required_frames(
                FakeConnection([b"S R=0,M=0\r\n"]), 0.4, StepClock()
            )


if __name__ == "__main__":
    unittest.main()
