from __future__ import annotations

"""Safe, read-only bring-up check for the Raspberry Pi to STM32 UART link."""

import argparse
import sys
import time
from typing import Callable, List, Optional, Protocol, Sequence, Set

import serial
from serial import SerialException

from .protocol import parse_frame


DEFAULT_PORT = "/dev/serial0"
DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 3.0
SERIAL_READ_TIMEOUT_S = 0.1
SERIAL_WRITE_TIMEOUT_S = 0.2
REQUIRED_FRAME_GROUPS = {
    "state": frozenset(("S", "STATE")),
    "config": frozenset(("CFG", "CONFIG")),
}


class ReadlineConnection(Protocol):
    def readline(self) -> bytes: ...


class SmokeTestError(RuntimeError):
    """The UART opened, but a valid GET ALL response was not observed."""


def wait_for_required_frames(
    connection: ReadlineConnection,
    timeout_s: float,
    clock: Callable[[], float] = time.monotonic,
) -> List[str]:
    """Collect lines until both the state and configuration frames arrive."""

    if timeout_s <= 0.0:
        raise ValueError("timeout_s must be positive")

    deadline = clock() + timeout_s
    received: List[str] = []
    seen_groups: Set[str] = set()
    while clock() < deadline:
        raw = connection.readline()
        if not raw:
            continue
        try:
            line = raw.decode("ascii").strip()
        except UnicodeDecodeError as exc:
            raise SmokeTestError("received non-ASCII data; check baud and wiring") from exc
        if not line:
            continue
        received.append(line)
        prefix, _fields = parse_frame(line)
        for group, prefixes in REQUIRED_FRAME_GROUPS.items():
            if prefix in prefixes:
                seen_groups.add(group)
        if len(seen_groups) == len(REQUIRED_FRAME_GROUPS):
            return received

    missing = sorted(set(REQUIRED_FRAME_GROUPS) - seen_groups)
    raise SmokeTestError("timeout waiting for frame(s): " + ", ".join(missing))


def run_smoke_test(port: str, baud: int, timeout_s: float) -> Sequence[str]:
    """Open an explicit 8N1 link, issue GET ALL, and validate its two replies."""

    if not port:
        raise ValueError("port must not be empty")
    if baud <= 0:
        raise ValueError("baud must be positive")

    with serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=SERIAL_READ_TIMEOUT_S,
        write_timeout=SERIAL_WRITE_TIMEOUT_S,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as connection:
        connection.reset_input_buffer()
        connection.write(b"GET ALL\r\n")
        connection.flush()
        return wait_for_required_frames(connection, timeout_s)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Verify the STM32F407 GET ALL response without starting the motors."
    )
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_argument_parser().parse_args(argv)
    try:
        lines = run_smoke_test(args.port, args.baud, args.timeout)
    except (ValueError, SmokeTestError, SerialException, OSError) as exc:
        print(f"UART smoke test failed: {exc}", file=sys.stderr)
        return 1

    print(f"UART smoke test passed on {args.port} at {args.baud} baud")
    for line in lines:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
