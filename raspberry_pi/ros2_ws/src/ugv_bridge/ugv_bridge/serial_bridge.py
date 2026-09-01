from __future__ import annotations

# ROS2 到 STM32 文本串口协议的桥接节点。
#
# 节点订阅 /cmd_vel，把线速度/角速度换算为左右 PWM 周期下发；同时发布
# STM32 遥测、故障、电池和急停。所有会改变底盘运行状态的操作
# 都要经过 STM32 的 OK/ERR ACK，避免 ROS2 侧误以为车辆已经执行命令。

import threading
import time
from dataclasses import dataclass, field
from typing import Dict, Optional, Tuple

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from std_msgs.msg import Bool, Float32, String, UInt32
from std_srvs.srv import Trigger

import serial
from serial import SerialException

from .protocol import parse_ack, parse_bool_field, parse_frame, parse_int_field, twist_to_pwm


SERIAL_READ_TIMEOUT_S = 0.1
SERIAL_WRITE_TIMEOUT_S = 0.2
SERIAL_RECONNECT_DELAY_S = 1.0
COMMAND_ACK_TIMEOUT_S = 0.6
READER_JOIN_TIMEOUT_S = 1.0
MILLIVOLTS_PER_VOLT = 1000.0


@dataclass
class PendingAck:
    """等待一个命令回执，由串口读线程唤醒 ROS2 服务回调线程。"""

    event: threading.Event = field(default_factory=threading.Event)
    success: bool = False
    message: str = "timeout"


class UgVSerialBridge(Node):
    """在 ROS2 话题/服务与 STM32 文本串口协议之间进行双向桥接。"""

    def __init__(self) -> None:
        super().__init__("ugv_serial_bridge")
        self._declare_parameters()
        command_rate_hz = self._load_parameters()

        self._serial: Optional[serial.Serial] = None
        # serial_lock 保护串口对象生命周期；pending_lock 保护 ACK 等待表。
        self._serial_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._pending: Dict[str, PendingAck] = {}
        self._stop_reader = threading.Event()
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._armed = False
        self._last_cmd_time = time.monotonic()
        self._requested_pwm: Tuple[int, int] = (0, 0)

        self._create_ros_interfaces(command_rate_hz)
        self._reader.start()

    def _declare_parameters(self) -> None:
        """集中声明外部配置，参数名与 YAML 文件保持一一对应。"""
        self.declare_parameter("port", "/dev/serial0")
        self.declare_parameter("baud", 115200)
        self.declare_parameter("command_rate_hz", 10.0)
        self.declare_parameter("cmd_vel_timeout_s", 0.25)
        self.declare_parameter("wheel_base_m", 0.28)
        self.declare_parameter("max_wheel_speed_mps", 0.80)

    def _load_parameters(self) -> float:
        """读取并校验参数，返回创建周期定时器所需的发送频率。"""
        self._port = str(self.get_parameter("port").value)
        self._baud = int(self.get_parameter("baud").value)
        rate = float(self.get_parameter("command_rate_hz").value)
        self._cmd_timeout = float(self.get_parameter("cmd_vel_timeout_s").value)
        self._wheel_base = float(self.get_parameter("wheel_base_m").value)
        self._max_wheel_speed = float(self.get_parameter("max_wheel_speed_mps").value)
        if self._baud <= 0 or rate <= 0.0 or self._cmd_timeout <= 0.0:
            raise ValueError("baud, command_rate_hz and cmd_vel_timeout_s must be positive")
        return rate

    def _create_ros_interfaces(self, command_rate_hz: float) -> None:
        """创建话题、服务和定时器；此方法不启动后台读线程。"""
        self._raw_pub = self.create_publisher(String, "ugv/telemetry_raw", 20)
        self._fault_pub = self.create_publisher(UInt32, "ugv/fault_flags", 10)
        self._battery_pub = self.create_publisher(Float32, "ugv/battery_voltage", 10)
        self._estop_pub = self.create_publisher(Bool, "ugv/estop", 10)
        self.create_subscription(Twist, "cmd_vel", self._on_cmd_vel, 10)
        self.create_service(Trigger, "ugv/start", self._on_start)
        self.create_service(Trigger, "ugv/stop", self._on_stop)
        self.create_service(Trigger, "ugv/clear_faults", self._on_clear_faults)
        self.create_timer(1.0 / command_rate_hz, self._command_timer)

    def _connect(self) -> bool:
        # 确保串口已打开；失败时让读线程稍后重试。
        if self._serial is not None and self._serial.is_open:
            return True
        try:
            connection = serial.Serial(
                port=self._port,
                baudrate=self._baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=SERIAL_READ_TIMEOUT_S,
                write_timeout=SERIAL_WRITE_TIMEOUT_S,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
            with self._serial_lock:
                self._serial = connection
            self.get_logger().info(f"Connected to STM32 on {self._port} at {self._baud} baud")
            # 连接恢复后先拉一次整车状态，给上层话题和日志一个同步点。
            self._write_line("GET ALL")
            return True
        except (SerialException, OSError) as exc:
            self.get_logger().warning(f"STM32 serial connection failed: {exc}")
            return False

    def _disconnect(self) -> None:
        # 断开串口并解除 armed，防止重连前继续认为底盘可控。
        self._disarm()
        with self._serial_lock:
            connection = self._serial
            self._serial = None
        if connection is not None:
            try:
                connection.close()
            except (SerialException, OSError):
                pass

    def _write_line(self, command: str) -> bool:
        # 写一行 ASCII 命令；写失败会主动断链并让读线程重连。
        payload = (command.strip() + "\r\n").encode("ascii")
        with self._serial_lock:
            connection = self._serial
            if connection is None or not connection.is_open:
                return False
            try:
                connection.write(payload)
                return True
            except (SerialException, OSError):
                pass
        self._disconnect()
        return False

    def _disarm(self) -> None:
        # ROS2 侧解除运行授权；STM32 仍由自身安全链负责最终停车。
        self._armed = False
        self._requested_pwm = (0, 0)

    def _command_with_ack(
        self,
        command: str,
        ack_name: str,
        timeout: float = COMMAND_ACK_TIMEOUT_S,
    ) -> Tuple[bool, str]:
        # 发送命令并等待指定 C 字段的 OK/ERR，服务回调用它同步返回结果。
        pending = PendingAck()
        key = ack_name.upper()
        with self._pending_lock:
            if key in self._pending:
                # 同名命令同时等待会让 ACK 无法区分归属，直接拒绝第二个请求。
                return False, "command already pending"
            self._pending[key] = pending
        try:
            if not self._write_line(command):
                return False, "serial disconnected"
            if not pending.event.wait(timeout):
                return False, "STM32 response timeout"
            return pending.success, pending.message
        finally:
            with self._pending_lock:
                self._pending.pop(key, None)

    def _reader_loop(self) -> None:
        # 后台读线程：负责自动重连、按行读取、把 ACK/遥测分发出去。
        while not self._stop_reader.is_set():
            if not self._connect():
                self._stop_reader.wait(SERIAL_RECONNECT_DELAY_S)
                continue
            try:
                assert self._serial is not None
                raw = self._serial.readline()
                if raw:
                    self._handle_line(raw.decode("ascii", errors="replace").strip())
            except (SerialException, OSError) as exc:
                self.get_logger().error(f"STM32 serial link lost: {exc}")
                self._disconnect()

    def _handle_line(self, line: str) -> None:
        # 处理 STM32 的一行输出；ACK 优先结算，遥测再发布到 ROS2。
        if not line:
            return
        raw_message = String()
        raw_message.data = line
        self._raw_pub.publish(raw_message)

        ack = parse_ack(line)
        if ack is not None:
            success, command, message = ack
            with self._pending_lock:
                pending = self._pending.get(command)
                if pending is not None:
                    pending.success = success
                    pending.message = "ok" if success else (message or "rejected")
                    pending.event.set()
            return

        prefix, fields = parse_frame(line)
        if prefix not in ("T", "S"):
            return
        self._publish_fault(fields, line)
        self._publish_battery(fields, line)
        self._publish_estop(fields, line)

    def _publish_fault(self, fields: Dict[str, str], source_line: str) -> None:
        """发布故障位；格式非法时保守丢弃该字段并保留原始帧。"""
        if "F" in fields:
            faults = parse_int_field(fields, "F")
            if faults is None:
                self.get_logger().warning(f"Malformed STM32 fault field: {source_line}")
            else:
                message = UInt32()
                message.data = faults
                self._fault_pub.publish(message)
                if faults != 0:
                    # STM32 已经锁存故障，ROS2 侧停止继续下发旧的运动需求。
                    self._disarm()

    def _publish_battery(self, fields: Dict[str, str], source_line: str) -> None:
        """把固件上报的毫伏值转换为伏特后发布。"""
        if "BV" in fields:
            battery_mv = parse_int_field(fields, "BV")
            if battery_mv is None:
                self.get_logger().warning(f"Malformed STM32 battery field: {source_line}")
            else:
                message = Float32()
                message.data = battery_mv / MILLIVOLTS_PER_VOLT
                self._battery_pub.publish(message)

    def _publish_estop(self, fields: Dict[str, str], source_line: str) -> None:
        """发布急停状态，只接受协议工具明确识别的布尔文本。"""
        if "ES" in fields:
            estop = parse_bool_field(fields, "ES")
            if estop is None:
                self.get_logger().warning(f"Malformed STM32 estop field: {source_line}")
            else:
                message = Bool()
                message.data = estop
                self._estop_pub.publish(message)

    def _on_cmd_vel(self, message: Twist) -> None:
        # 缓存最新速度指令；真正下发由固定频率 timer 完成。
        self._requested_pwm = twist_to_pwm(
            message.linear.x,
            message.angular.z,
            self._wheel_base,
            self._max_wheel_speed,
        )
        self._last_cmd_time = time.monotonic()

    def _command_timer(self) -> None:
        # 运行期周期下发 PWM；cmd_vel 超时后主动发 0，而不是沿用旧速度。
        if not self._armed:
            return
        if (time.monotonic() - self._last_cmd_time) > self._cmd_timeout:
            left_pwm, right_pwm = 0, 0
        else:
            left_pwm, right_pwm = self._requested_pwm
        if not self._write_line(f"PWM {left_pwm} {right_pwm}"):
            self._armed = False

    def _on_clear_faults(self, _request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
        response.success, response.message = self._command_with_ack("FAULT CLEAR", "FAULT")
        return response

    def _on_start(self, _request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
        # 启动前先尝试清故障；START 成功后才进入 armed 周期输出。
        cleared, message = self._command_with_ack("FAULT CLEAR", "FAULT")
        if not cleared:
            response.success = False
            response.message = f"fault clear failed: {message}"
            return response
        response.success, response.message = self._command_with_ack("START", "START")
        if response.success:
            self._requested_pwm = (0, 0)
            self._last_cmd_time = time.monotonic()
            self._armed = True
        return response

    def _on_stop(self, _request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
        self._disarm()
        response.success, response.message = self._command_with_ack("STOP", "STOP")
        return response

    def destroy_node(self) -> bool:
        self._disarm()
        self._write_line("STOP")
        self._stop_reader.set()
        self._reader.join(timeout=READER_JOIN_TIMEOUT_S)
        self._disconnect()
        return super().destroy_node()


def main(args=None) -> None:
    rclpy.init(args=args)
    node = UgVSerialBridge()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
