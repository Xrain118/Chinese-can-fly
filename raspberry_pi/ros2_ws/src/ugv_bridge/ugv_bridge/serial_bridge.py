from __future__ import annotations

import threading
import time
import math
from typing import Dict, Optional, Tuple

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node
from sensor_msgs.msg import Imu
from std_msgs.msg import Bool, Float32, String, UInt32
from std_srvs.srv import Trigger

import serial
from serial import SerialException

from .protocol import parse_ack, parse_bool_field, parse_frame, parse_int_field, twist_to_pwm


class PendingAck:
    def __init__(self) -> None:
        self.event = threading.Event()
        self.success = False
        self.message = "timeout"


class UgVSerialBridge(Node):
    def __init__(self) -> None:
        super().__init__("ugv_serial_bridge")
        self.declare_parameter("port", "/dev/serial0")
        self.declare_parameter("baud", 115200)
        self.declare_parameter("command_rate_hz", 10.0)
        self.declare_parameter("cmd_vel_timeout_s", 0.25)
        self.declare_parameter("wheel_base_m", 0.28)
        self.declare_parameter("max_wheel_speed_mps", 0.80)
        self.declare_parameter("imu_frame_id", "imu_link")

        self._port = str(self.get_parameter("port").value)
        self._baud = int(self.get_parameter("baud").value)
        rate = float(self.get_parameter("command_rate_hz").value)
        self._cmd_timeout = float(self.get_parameter("cmd_vel_timeout_s").value)
        self._wheel_base = float(self.get_parameter("wheel_base_m").value)
        self._max_wheel_speed = float(self.get_parameter("max_wheel_speed_mps").value)
        self._imu_frame_id = str(self.get_parameter("imu_frame_id").value)
        if rate <= 0.0 or self._cmd_timeout <= 0.0:
            raise ValueError("command_rate_hz and cmd_vel_timeout_s must be positive")

        self._serial: Optional[serial.Serial] = None
        self._serial_lock = threading.Lock()
        self._pending_lock = threading.Lock()
        self._pending: Dict[str, PendingAck] = {}
        self._stop_reader = threading.Event()
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._armed = False
        self._last_cmd_time = time.monotonic()
        self._requested_pwm: Tuple[int, int] = (0, 0)

        self._raw_pub = self.create_publisher(String, "ugv/telemetry_raw", 20)
        self._fault_pub = self.create_publisher(UInt32, "ugv/fault_flags", 10)
        self._battery_pub = self.create_publisher(Float32, "ugv/battery_voltage", 10)
        self._estop_pub = self.create_publisher(Bool, "ugv/estop", 10)
        self._imu_pub = self.create_publisher(Imu, "imu/data_raw", 20)
        self.create_subscription(Twist, "cmd_vel", self._on_cmd_vel, 10)
        self.create_service(Trigger, "ugv/start", self._on_start)
        self.create_service(Trigger, "ugv/stop", self._on_stop)
        self.create_service(Trigger, "ugv/clear_faults", self._on_clear_faults)
        self.create_timer(1.0 / rate, self._command_timer)
        self._reader.start()

    def _connect(self) -> bool:
        if self._serial is not None and self._serial.is_open:
            return True
        try:
            connection = serial.Serial(self._port, self._baud, timeout=0.1)
            with self._serial_lock:
                self._serial = connection
            self.get_logger().info(f"Connected to STM32 on {self._port} at {self._baud} baud")
            self._write_line("GET ALL")
            return True
        except (SerialException, OSError) as exc:
            self.get_logger().warning(f"STM32 serial connection failed: {exc}")
            return False

    def _disconnect(self) -> None:
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
        self._armed = False
        self._requested_pwm = (0, 0)

    def _command_with_ack(self, command: str, ack_name: str, timeout: float = 0.6) -> Tuple[bool, str]:
        pending = PendingAck()
        key = ack_name.upper()
        with self._pending_lock:
            if key in self._pending:
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
        while not self._stop_reader.is_set():
            if not self._connect():
                self._stop_reader.wait(1.0)
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
        if prefix == "I":
            self._publish_imu(fields, line)
            return
        if prefix not in ("T", "S"):
            return
        if "F" in fields:
            faults = parse_int_field(fields, "F")
            if faults is None:
                self.get_logger().warning(f"Malformed STM32 fault field: {line}")
            else:
                message = UInt32()
                message.data = faults
                self._fault_pub.publish(message)
                if faults != 0:
                    self._disarm()
        if "BV" in fields:
            battery_mv = parse_int_field(fields, "BV")
            if battery_mv is None:
                self.get_logger().warning(f"Malformed STM32 battery field: {line}")
            else:
                message = Float32()
                message.data = battery_mv / 1000.0
                self._battery_pub.publish(message)
        if "ES" in fields:
            estop = parse_bool_field(fields, "ES")
            if estop is None:
                self.get_logger().warning(f"Malformed STM32 estop field: {line}")
            else:
                message = Bool()
                message.data = estop
                self._estop_pub.publish(message)

    def _publish_imu(self, fields: Dict[str, str], line: str) -> None:
        required = ("AX", "AY", "AZ", "GX", "GY", "GZ")
        if any(key not in fields for key in required):
            self.get_logger().warning(f"Incomplete STM32 IMU frame: {line}")
            return
        try:
            # Firmware writes ACCEL_CONFIG0/GYRO_CONFIG0 as 0x06: ODR=1 kHz and
            # FS_SEL=0, which is +/-16 g and +/-2000 dps on ICM42688.
            accel_scale = 16.0 * 9.80665 / 32768.0
            gyro_scale = 2000.0 * math.pi / 180.0 / 32768.0
            message = Imu()
            message.header.stamp = self.get_clock().now().to_msg()
            message.header.frame_id = self._imu_frame_id
            message.orientation_covariance[0] = -1.0
            message.linear_acceleration.x = int(fields["AX"], 0) * accel_scale
            message.linear_acceleration.y = int(fields["AY"], 0) * accel_scale
            message.linear_acceleration.z = int(fields["AZ"], 0) * accel_scale
            message.angular_velocity.x = int(fields["GX"], 0) * gyro_scale
            message.angular_velocity.y = int(fields["GY"], 0) * gyro_scale
            message.angular_velocity.z = int(fields["GZ"], 0) * gyro_scale
            self._imu_pub.publish(message)
        except ValueError:
            self.get_logger().warning(f"Malformed STM32 IMU frame: {line}")

    def _on_cmd_vel(self, message: Twist) -> None:
        self._requested_pwm = twist_to_pwm(
            message.linear.x,
            message.angular.z,
            self._wheel_base,
            self._max_wheel_speed,
        )
        self._last_cmd_time = time.monotonic()

    def _command_timer(self) -> None:
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
        self._reader.join(timeout=1.0)
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
