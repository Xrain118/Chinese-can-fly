from __future__ import annotations

# STM32 文本协议的纯函数工具。
#
# 这里不依赖 ROS2 或串口对象，方便单元测试覆盖协议解析和 cmd_vel 到 PWM
# 的换算。serial_bridge.py 负责线程、服务和实际 I/O。

from typing import Dict, Optional, Tuple


PWM_MAX = 1000


def clamp(value: float, minimum: float, maximum: float) -> float:
    # 把数值限制到闭区间内，用于 PWM 饱和保护。
    return max(minimum, min(maximum, value))


def twist_to_pwm(
    linear_mps: float,
    angular_rps: float,
    wheel_base_m: float,
    max_wheel_speed_mps: float,
) -> Tuple[int, int]:
    # 把 ROS2 Twist 转成 STM32 使用的左右侧 signed PWM。
    # 差速模型：左轮线速度 = v - w * 轮距 / 2，右轮线速度 = v + w * 轮距 / 2。
    # max_wheel_speed_mps 是实测 PWM=1000 时的车轮线速度，不是电机空载转速。
    if wheel_base_m <= 0.0 or max_wheel_speed_mps <= 0.0:
        raise ValueError("wheel_base_m and max_wheel_speed_mps must be positive")

    left_mps = linear_mps - angular_rps * wheel_base_m * 0.5
    right_mps = linear_mps + angular_rps * wheel_base_m * 0.5
    scale = PWM_MAX / max_wheel_speed_mps
    left_pwm = round(clamp(left_mps * scale, -PWM_MAX, PWM_MAX))
    right_pwm = round(clamp(right_mps * scale, -PWM_MAX, PWM_MAX))
    return int(left_pwm), int(right_pwm)


def parse_frame(line: str) -> Tuple[str, Dict[str, str]]:
    # 解析一行 STM32 帧，返回大写前缀和 KEY=VALUE 字段表。
    text = line.strip()
    if not text:
        return "", {}
    prefix, separator, payload = text.partition(" ")
    if not separator:
        return prefix.upper(), {}

    fields: Dict[str, str] = {}
    # 固件可能用逗号或空格分隔字段；这里统一处理，避免上层关心线格式细节。
    for item in payload.replace(",", " ").split():
        key, equals, value = item.partition("=")
        if equals and key:
            fields[key.upper()] = value
    return prefix.upper(), fields


def parse_ack(line: str) -> Optional[Tuple[bool, str, str]]:
    # 解析 OK/ERR 回执；不是回执帧时返回 None。
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
