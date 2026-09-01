# Raspberry Pi UGV Setup

## STM32 UART wiring and OS setup

Connect the 3.3 V UART directly and cross TX/RX:

| STM32F407 | Raspberry Pi |
| --- | --- |
| PD5 / USART2_TX | GPIO15 / RXD0, physical pin 10 |
| PD6 / USART2_RX | GPIO14 / TXD0, physical pin 8 |
| GND | GND |

Do not connect either UART pin to 5 V. Keep these wires away from the motor power
path. On Raspberry Pi OS, run `sudo raspi-config`, open **Interface Options → Serial
Port**, disable the login shell on the serial port, and enable the serial hardware.
Reboot, then verify the stable UART alias and user permission:

```bash
ls -l /dev/serial0
sudo usermod -a -G dialout "$USER"
```

Log out and back in after changing the group. The STM32 USART2 side uses 115200
baud, 8 data bits, no parity, one stop bit, and no flow control.

## ROS2 serial bridge

Install ROS2 and the serial dependency, then build the package:

```bash
sudo apt install python3-serial python3-setuptools python3-colcon-common-extensions
cd ~/Chinesecanfly/raspberry_pi/ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 run ugv_bridge serial_smoke_test --port /dev/serial0 --baud 115200
ros2 launch ugv_bridge ugv_bridge.launch.py
```

The smoke test only sends `GET ALL` and succeeds after receiving both `S` and
`CFG`; it never sends `START` or a motor command. Run it before starting ROS2.

Edit `config/ugv_bridge.yaml` before use. `wheel_base_m` is the left-to-right
wheel contact distance. `max_wheel_speed_mps` is the measured vehicle speed at
PWM 1000, not the motor's unloaded shaft speed.

The bridge does not start the chassis automatically. Use:

```bash
ros2 service call /ugv/clear_faults std_srvs/srv/Trigger
ros2 service call /ugv/start std_srvs/srv/Trigger
ros2 service call /ugv/stop std_srvs/srv/Trigger
```

Nav2 or a teleoperation node publishes `geometry_msgs/Twist` on `/cmd_vel`.
The bridge publishes raw telemetry, fault flags, battery voltage, and e-stop
state on `/ugv/telemetry_raw`, `/ugv/fault_flags`, `/ugv/battery_voltage`, and
`/ugv/estop`.

The bridge sends wheel commands at 10 Hz. While the bridge remains running, a
stale `/cmd_vel` becomes zero PWM after 250 ms. The STM32 firmware does **not**
currently implement a communication watchdog: if the bridge process or UART link
fails completely after a nonzero command, automatic stopping is not guaranteed.
Use a physical emergency stop and do not operate the chassis unattended.

## Wi-Fi hotspot

NetworkManager on Raspberry Pi OS can create the UAV-facing hotspot:

```bash
chmod +x setup_hotspot.sh
sudo ./setup_hotspot.sh UGV-Hotspot UGV-Rescue 'replace-with-a-strong-password' wlan0
```

Use the same ROS domain ID on the Raspberry Pi and UAV computer. For reliable
ROS2 DDS discovery, allow multicast and UDP traffic on the hotspot interface.
