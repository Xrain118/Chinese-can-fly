# 八路循迹蓝牙调试台（STM32 四驱版）

本目录是 STM32F407VET6 四驱循迹小车的网页调参工具，由 MSPM0 版调试台移植而来。通过蓝牙透传模块把小车当作一个串口连接，用浏览器实时读取遥测、调节 PID 并控制运行。

## 打开方式

1. 用**桌面版 Chrome 或 Edge** 打开 `index.html`（直接双击、`file://` 即可，无需服务器）。
2. 电脑与 HC-04/HC-05 配对后会多出一个 COM 口（或 macOS/Linux 的 `/dev/tty.*`）。
3. 点击"连接串口"选择该串口，波特率默认 **115200 8N1**（与 `STM32/Hardware/Serial.c` 的 USART2 PD5/PD6 配置一致）。

连接成功后页面会自动发送 `GET ALL` 读取整车参数，并接收小车约 20 Hz 的遥测帧。

## 页面功能

- **运动控制**：START/STOP；直接 PWM/直行模式；左右各一套但共享参数的速度 PI；左右目标差同步 P；四个车轮 CPS 诊断。
- **IMU**：显示固件 `I` 帧中的 AX/AY/AZ/GX/GY/GZ/TEMP 原始值；当前固件不输出姿态角。
- **串口数据**：终端 + 手动指令 + 常用指令按钮 + 命令回执 toast。
- **PID 图表**：点击图表按钮在独立窗口实时绘制遥测曲线（8 路传感器、左右 PWM/编码器、PID 派生量等）。
- **预设管理**：本机保存/导入/导出 JSON 预设，逐项确认地下发整套参数，或用"小车当前值"从 CFG 帧一键建立预设。

## 协议契约（固件侧按此实现）

文本协议，每行以 CRLF 结尾。字段为 `KEY=VALUE`，逗号分隔。

### 小车 → 页面（RX）

| 前缀 | 说明 |
| --- | --- |
| `T k=v,...` / `TEL` | 周期遥测，建议 20 Hz。`VLF/VLR/VRF/VRR` 为四轮 CPS，`EL/ER` 为左右侧平均 CPS，`TL/TR` 为目标 CPS，`ED/ESC/ESA` 为同步误差/修正/活动状态 |
| `S k=v,...` / `STATE` | GET ALL 的运行快照响应，字段与 `T` 基本一致 |
| `CFG k=v,...` / `CONFIG` | GET ALL 的配置响应，包含 `EC/EKP/EKI/EFS/ECL/ESE/ESKP/EST/ESL/WDT/BLV` |
| `I AX=,AY=,AZ=,GX=,GY=,GZ=,TEMP=` / `IMU` | IMU 原始帧 |
| `OK C=<CMD>` | 命令确认回执 |
| `ERR C=<CMD>` | 命令拒绝回执 |

页面收到 `CFG` 后才会结算 `GET ALL` 并标记配置同步；单独的 `S` 帧只更新运行状态。

### 页面 → 小车（TX）

```
START / STOP
MODE DIRECT | STRAIGHT
PWM left right        MOVE left right  SPEED n
ENC ON | OFF          ENC PID kp ki    ENC CPS n    ENC LIMIT n
ENC SYNC ON | OFF     ENC SYNC kp toleranceCps limitPwm
GET ALL               RESET            DEFAULTS
PING                  HEARTBEAT        FAULT CLEAR
```

默认基线：SPEED 0、ENC OFF、ENC PID 0.02/0、CPS 5000、ENC LIMIT 100、SYNC OFF、同步 P 0.01、容差 50 CPS、限幅 50 PWM。`MODE TRACK` 会返回 `ERR C=MODE,M=TRACK_REMOVED`，旧页面上的循迹/角度控件仅作为兼容占位。

左前/左后和右前/右后的编码器分别测量，但同侧两台电机共用一个 PWM。四轮 CPS 的同侧差值用于诊断负载、打滑和编码器方向，不代表具备四路独立轮速闭环能力。

## 页面占位

- **姿态角未接入**：固件只发送原始 IMU；页面不再从 `I` 帧推断 Roll/Pitch/Yaw。
- **编码器默认关闭**：页面初始为 ENC OFF（直接 PWM 开环），接好编码器后再按 `ENC ON` 闭环。

## 文件结构

```
web\
├── index.html              主页面（运动控制 / 陀螺仪 / 串口数据 三个页签）
├── TrackingPidChart.html   PID 实时图表弹窗（BroadcastChannel 通信）
├── css\styles.css
└── js\
    ├── state.js            全局状态、元素引用、别名表
    ├── serial.js           串口读写、命令事务、自动重连
    ├── telemetry.js        帧解析与界面回填
    ├── ui.js               界面渲染与 PID 图表快照
    ├── controls.js         按钮与输入监听
    ├── config.js           预设管理（v2 JSON 格式，兼容导入 v1）
    └── bootstrap.js        启动初始化
```
