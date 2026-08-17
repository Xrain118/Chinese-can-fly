# 八路循迹蓝牙调试台（STM32 四驱版）

本目录是 STM32F103C8 四驱循迹小车的网页调参工具，由 MSPM0 版调试台移植而来。通过蓝牙透传模块（HC-04 / HC-05）把小车当作一个串口连接，用浏览器实时读取遥测、调节 PID 并控制运行。

## 打开方式

1. 用**桌面版 Chrome 或 Edge** 打开 `index.html`（直接双击、`file://` 即可，无需服务器）。
2. 电脑与 HC-04/HC-05 配对后会多出一个 COM 口（或 macOS/Linux 的 `/dev/tty.*`）。
3. 点击"连接串口"选择该串口，波特率默认 **9600 8N1**（与 `STM32/Hardware/Serial.c` 的 USART1 PA9/PA10 配置一致）。

连接成功后页面会自动发送 `GET ALL` 读取整车参数，并接收小车约 20 Hz 的遥测帧。

## 页面功能

- **运动控制**：START/STOP；三种模式（循迹 TRACK / 直行 STRAIGHT / 角度 ANGLE）；循迹 PID（kp/ki/kd）+ 输出限幅 LIMIT + 基础速度 SPEED；**八路**循迹权重（CH1 左 → CH8 右，逐路或一次 8 路发送）；编码器 PI（ENC ON/OFF、PID、CPS 满量程、限幅）；角度 PID（目标角度、kp/ki/kd、最小/最大转向 PWM、到位判定容差与时间、车端归零）。
- **陀螺仪**：IMU 3D 姿态模型（可拖动视角、以当前姿态归零）与车端角度控制状态。
- **串口数据**：终端 + 手动指令 + 常用指令按钮 + 命令回执 toast。
- **PID 图表**：点击图表按钮在独立窗口实时绘制遥测曲线（8 路传感器、左右 PWM/编码器、PID 派生量等）。
- **预设管理**：本机保存/导入/导出 JSON 预设，逐项确认地下发整套参数，或用"小车当前值"从 STATE 帧一键建立预设。

## 协议契约（固件侧按此实现）

文本协议，每行以 CRLF 结尾。字段为 `KEY=VALUE`，逗号分隔。

### 小车 → 页面（RX）

| 前缀 | 说明 |
| --- | --- |
| `T k=v,...` / `TEL` | 周期遥测，建议 20 Hz。`S=` 为 **8 字符 0/1 串**（CH1 在最高位），`PL`/`PR` 左右 PWM，`EL`/`ER` 左右编码器，`R` 运行、`M` 模式、`E` 错误码，角度字段 `AH/AE/AO/AS/AR` |
| `S k=v,...` / `STATE` | GET ALL 的响应：全部配置与状态，含 `KP/KI/KD/SP/L/W1..W8/EC/EKP/EKI/EFS/ECL/AKP/AKI/AKD/AT/AMIN/AMAX/ATOL/ASET/AZ/AS/AR` |
| `I R=,P=,Y=` / `IMU` | 姿态帧（roll/pitch/yaw，度） |
| `OK C=<CMD>` | 命令确认回执 |
| `ERR C=<CMD>` | 命令拒绝回执 |

S 帧本身也会结算页面发出的 `GET ALL`，所以固件也可以只回 S 帧、不发 OK。

### 页面 → 小车（TX）

```
START / STOP
MODE TRACK | STRAIGHT | ANGLE
PID kp ki kd          SPEED n          LIMIT n
WEIGHT ch n           WEIGHTS w1..w8
ENC ON | OFF          ENC PID kp ki    ENC CPS n    ENC LIMIT n
ANGLE TARGET t        ANGLE PID kp ki kd
ANGLE PWM min max     ANGLE SETTLE tol ms
ANGLE ZERO
GET ALL               RESET            DEFAULTS
```

权重必须从 CH1 到 CH8 严格递增、±10000 内。页面默认基线（也是将来固件 DEFAULTS 的建议基线）：SPEED 400、PID 0.14/0/0.00025、LIMIT 280、权重 −7000/−5000/−3000/−1000/1000/3000/5000/7000、角度 3/0/0.8、AMIN 100/AMAX 350、ATOL 2/ASET 250、ENC OFF、ENC PID 0.02/0、CPS 5000、ENC LIMIT 100。

## 尚未接入的硬件（页面已按占位处理）

- **IMU 未接入**：固件不要发送 `I` 帧、也不要发送 `AH/AZ` 等角度字段，页面会显示"等待遥测"与"等待车端零位"；归零按钮不可用。
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
    ├── config.js           预设管理（v1 JSON 格式）
    └── bootstrap.js        启动初始化
```
