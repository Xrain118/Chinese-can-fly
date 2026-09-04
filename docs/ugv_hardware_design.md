# High-Level UGV Hardware Design

## System Architecture

This design targets the advanced disaster-search-and-delivery track with a
four-wheel differential unmanned ground vehicle.

- STM32F407VET6 is the real-time motion controller.
- Raspberry Pi is the onboard computer for ROS2, Wi-Fi hotspot, lidar, mapping,
  localization, path planning, and air-ground target exchange.
- Two external TB6612 modules drive the four wheel motors. One module is
  assigned to the front axle, and one module is assigned to the rear axle.

## STM32F407 Pin Allocation

### Dual External TB6612 Motor Drive

Motor numbering is Motor1=left-front, Motor2=right-front, Motor3=left-rear,
and Motor4=right-rear. PWM is generated at 20 kHz by TIM10, TIM11, and TIM2.

| Module | Motor / channel | PWM | Direction inputs | Shared standby |
| --- | --- | --- | --- | --- |
| Front TB6612 | Motor1 / left-front / A | PB8 / TIM10_CH1 (PWMA) | PC8 (AIN1), PC9 (AIN2) | PC12 |
| Front TB6612 | Motor2 / right-front / B | PB9 / TIM11_CH1 (PWMB) | PC10 (BIN1), PC11 (BIN2) | PC12 |
| Rear TB6612 | Motor3 / left-rear / A | PB10 / TIM2_CH3 (PWMA) | PD10 (AIN1), PD11 (AIN2) | PE15 |
| Rear TB6612 | Motor4 / right-rear / B | PB11 / TIM2_CH4 (PWMB) | PD14 (BIN1), PD15 (BIN2) | PE15 |

All front-module signals are on the P2 header and all rear-module signals are
on P1. The baseboard motor-driver signals PE5/PE6 and PB14/PB15 are not used.
PC8 through PC12 are reassigned from the optional TF interface, so no TF card
may be fitted while this motor allocation is in use. Fit an external 10 kOhm
pull-down on each STBY signal so both modules stay disabled during reset.

### Encoders

| Motor / wheel | STM32 pins | Acquisition method |
| --- | --- | --- |
| Motor1 / left-front | PC6 / PC7 | TIM8_CH1/CH2 hardware encoder mode |
| Motor2 / right-front | PB4 / PB5 | TIM3_CH1/CH2 hardware encoder mode |
| Motor3 / left-rear | PD12 / PD13 | TIM4_CH1/CH2 hardware encoder mode |
| Motor4 / right-rear | PE9 / PE11 | TIM1_CH1/CH2 hardware encoder mode |

The front encoder signals are on P2 and the rear encoder signals are on P1.
PB5 and the PB8..PB11 PWM group replace their optional screen, audio, Ethernet,
or SPI functions; those optional functions cannot be enabled concurrently.

Four independent encoders are preferred. If the mechanical build only has two
encoders, install them on one wheel per side and leave the unused inputs open or
adapt firmware later.

### Bluetooth and Raspberry Pi UART Links

The two host links are independent and may receive the same text protocol. Replies
to commands and `GET ALL` are returned only to the port that issued the command;
periodic telemetry is transmitted on both ports.

| Purpose | STM32 interface | Host connection | Speed |
| --- | --- | --- | --- |
| HC-04/HC-05 tuning | PA9 / USART1_TX / U1T, PA10 / USART1_RX / U1R | Bluetooth module RXD/TXD | 9600 8N1 |
| Raspberry Pi race link | PD5 / USART2_TX, PD6 / USART2_RX | Pi GPIO15/RXD0, GPIO14/TXD0 | 115200 8N1 |

Direct Raspberry Pi wiring is crossed as follows:

| STM32F407 | Raspberry Pi header |
| --- | --- |
| PD5 / USART2_TX | GPIO15 / RXD0, physical pin 10 |
| PD6 / USART2_RX | GPIO14 / TXD0, physical pin 8 |
| GND | Any Pi GND pin |

Both devices use 3.3 V TTL levels. Never apply 5 V to PD5, PD6, or the Pi UART,
and always connect the grounds. Keep the UART wires short and away from motor leads.

The Pi can send `SPEED` while the vehicle is stopped; `START` then restores that
saved value as equal left/right base PWM. `STOP` disables the actual outputs but
keeps the last `SPEED`. A running vehicle may also accept `SPEED` in either drive
mode. The direct differential command format is:

```text
PWM <left_pwm> <right_pwm>
```

Both values are signed and limited to -1000..1000. Positive values drive the
vehicle forward after motor polarity has been calibrated.

## Power Plan

- Main battery: choose 2S or 3S lithium according to the motor voltage.
- Motor VM: route through a power switch, fuse, bulk capacitor, and TVS diode.
- TB6612 logic VCC: use 3.3 V to match STM32 IO directly.
- STM32: stable 3.3 V rail with local decoupling.
- Raspberry Pi: independent 5 V regulator rated at 3 A minimum; use more margin
  if the lidar is powered from the Pi USB port.
- All grounds must be common, but high-current motor return should not pass
  through the STM32 ground path.

## Raspberry Pi Role

- Runs ROS2.
- Opens the Wi-Fi hotspot used by the UAV onboard computer.
- Receives target class, target position, confidence, mission id, and heartbeat
  over ROS2 DDS.
- Reads the 2D lidar through USB.
- Runs localization, mapping, obstacle avoidance, and path planning.
- Converts planned motion into signed left/right PWM or a later velocity command
  and sends it to STM32 over UART.

## Bring-Up Checklist

1. Verify the STM32 clock and SWD download.
2. Verify USART1 at 9600 baud on U1T/U1R and USART2 at 115200 baud on PD5/PD6.
3. Run the Raspberry Pi read-only `serial_smoke_test` and verify `S` plus `CFG`.
4. Test each TB6612 channel independently with low PWM.
5. Calibrate motor direction signs in `Motor.h`.
6. Verify all encoder directions in `Encoder.h`.
7. Test `START`, `PWM 200 200`, `PWM 200 -200`, and `STOP` over UART.
8. Enable encoder closed-loop mode after open-loop motor direction is correct.
9. Start the Raspberry Pi hotspot and confirm the UAV computer can join it.
10. Confirm ROS2 DDS target messages between the UAV computer and the Pi.
11. Confirm lidar scan, localization, path planning, and UART motion output.
