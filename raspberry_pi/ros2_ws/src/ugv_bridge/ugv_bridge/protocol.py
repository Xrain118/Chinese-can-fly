from __future__ import annotations

from typing import Dict, Optional, Tuple


PWM_MAX = 1000


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def twist_to_pwm(
    linear_mps: float,
    angular_rps: float,
    wheel_base_m: float,
    max_wheel_speed_mps: float,
) -> Tuple[int, int]:
    if wheel_base_m <= 0.0 or max_wheel_speed_mps <= 0.0:
        raise ValueError("wheel_base_m and max_wheel_speed_mps must be positive")

    left_mps = linear_mps - angular_rps * wheel_base_m * 0.5
    right_mps = linear_mps + angular_rps * wheel_base_m * 0.5
    scale = PWM_MAX / max_wheel_speed_mps
    left_pwm = round(clamp(left_mps * scale, -PWM_MAX, PWM_MAX))
    right_pwm = round(clamp(right_mps * scale, -PWM_MAX, PWM_MAX))
    return int(left_pwm), int(right_pwm)


def parse_frame(line: str) -> Tuple[str, Dict[str, str]]:
    text = line.strip()
    if not text:
        return "", {}
    prefix, separator, payload = text.partition(" ")
    if not separator:
        return prefix.upper(), {}

    fields: Dict[str, str] = {}
    for item in payload.replace(",", " ").split():
        key, equals, value = item.partition("=")
        if equals and key:
            fields[key.upper()] = value
    return prefix.upper(), fields


def parse_ack(line: str) -> Optional[Tuple[bool, str, str]]:
    prefix, fields = parse_frame(line)
    if prefix not in ("OK", "ERR") or "C" not in fields:
        return None
    return prefix == "OK", fields["C"].upper(), fields.get("M", "")


def parse_int_field(fields: Dict[str, str], key: str) -> Optional[int]:
    """字段非法时返回 None，由调用方跳过该字段而不是丢整帧。"""
    value = fields.get(key.upper())
    if value is None:
        return None
    try:
        return int(value, 0)
    except ValueError:
        return None


def parse_bool_field(fields: Dict[str, str], key: str) -> Optional[bool]:
    """只接受明确布尔值，避免把任意字符串隐式当成急停状态。"""
    value = fields.get(key.upper())
    if value is None:
        return None
    normalized = value.strip().upper()
    if normalized in ("1", "ON", "TRUE"):
        return True
    if normalized in ("0", "OFF", "FALSE"):
        return False
    return None
