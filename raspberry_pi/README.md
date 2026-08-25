# Raspberry Pi UGV Setup

## ROS2 serial bridge

Install ROS2 and the serial dependency, then build the package:

```bash
sudo apt install python3-serial python3-setuptools python3-colcon-common-extensions
cd ~/Chinesecanfly/raspberry_pi/ros2_ws
colcon build --symlink-install
source install/setup.bash
ros2 launch ugv_bridge ugv_bridge.launch.py
```

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

The bridge sends wheel commands at 10 Hz. A stale `/cmd_vel` becomes zero PWM
after 250 ms. If the bridge process or UART fails completely, STM32 stops the
chassis after its 300 ms communication timeout.

## Wi-Fi hotspot

NetworkManager on Raspberry Pi OS can create the UAV-facing hotspot:

```bash
chmod +x setup_hotspot.sh
sudo ./setup_hotspot.sh UGV-Hotspot UGV-Rescue 'replace-with-a-strong-password' wlan0
```

Use the same ROS domain ID on the Raspberry Pi and UAV computer. For reliable
ROS2 DDS discovery, allow multicast and UDP traffic on the hotspot interface.
