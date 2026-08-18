import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config_path = os.path.join(
        get_package_share_directory("ugv_bridge"), "config", "ugv_bridge.yaml"
    )
    return LaunchDescription(
        [
            Node(
                package="ugv_bridge",
                executable="serial_bridge",
                name="ugv_serial_bridge",
                output="screen",
                parameters=[config_path],
            )
        ]
    )
