from glob import glob
from setuptools import find_packages, setup


package_name = "ugv_bridge"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/launch", glob("launch/*.launch.py")),
        ("share/" + package_name + "/config", glob("config/*.yaml")),
    ],
    install_requires=["setuptools", "pyserial"],
    zip_safe=True,
    maintainer="UGV Team",
    maintainer_email="team@example.com",
    description="ROS2 serial bridge for the STM32F407 UGV chassis.",
    license="MIT",
    entry_points={"console_scripts": ["serial_bridge = ugv_bridge.serial_bridge:main"]},
)
