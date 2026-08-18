        "use strict";

        let serialPort = null;
        let preferredPort = null;
        let reader = null;
        let readLoopPromise = null;
        let connectAttemptPromise = null;
        let reconnectTimer = null;
        let heartbeatTimer = null;
        let reconnectAttempt = 0;
        let keepReading = false;
        let isDisconnecting = false;
        let manualDisconnect = false;
        let receiveBuffer = "";
        let writeChain = Promise.resolve();
        let logLineCount = 0;
        let nextCommandId = 1;
        let lastTelemetryAt = 0;
        let lastAttitudeAt = 0;
        let attitudeRateHz = 0;
        let attitudeHasData = false;
        let attitudeZeroed = false;
        /*
         * 角度控制运行状态严格以 MCU 的 S/T 帧为准。null 表示尚未收到对应字段，
         * 不能用浏览器接收的第一帧原始 Yaw 擅自建立另一个控制零位。
         */
        let currentAngleHeading = null;
        let currentAngleTarget = 0;
        let currentAngleError = 0;
        let currentAngleOutput = 0;
        let currentAngleState = 0;
        let angleControlReady = false;
        let angleZeroYaw = null;
        let currentDriveMode = 0;
        let encoderLoopEnabled = false;
        let encoderSyncEnabled = false;
        let encoderSyncActive = false;
        let configApplyRunning = false;
        let deviceConfigurationSynchronized = false;
        let vehicleConfigurationSynchronized = false;
        let currentPresetConfiguration = null;
        let presetDialogOpen = false;
        let pidChartWindow = null;
        let pidChartChannel = null;
        const pidChartSnapshot = {};
        const pidTelemetryState = {};
        let pidChartConnectionState = { connected: false, message: "未连接", reconnecting: false };

        const attitudeAngles = {
            roll: { raw: null, continuous: 0 },
            pitch: { raw: null, continuous: 0 },
            yaw: { raw: null, continuous: 0 }
        };
        const attitudeZero = { roll: 0, pitch: 0, yaw: 0 };
        const attitudeView = {
            pitch: 57,
            yaw: -42,
            lockedTop: false,
            pointerId: null,
            startX: 0,
            startY: 0,
            startPitch: 57,
            startYaw: -42
        };

        const MAX_LOG_LINES = 300;
        const MAX_RECEIVE_BUFFER = 8192;
        const MAX_COMMAND_TOASTS = 4;
        const COMMAND_TIMEOUT_MS = 5000;
        const HEARTBEAT_INTERVAL_MS = 200;
        const RECONNECT_DELAYS_MS = [400, 800, 1500, 2500, 4000];
        const AUTO_RECONNECT_STORAGE_KEY = "trackingDebugger.autoReconnect";
        const BAUD_RATE_STORAGE_KEY = "trackingDebugger.baudRate.f407";
        const CONSOLE_PAGE_STORAGE_KEY = "trackingDebugger.consolePage";
        /* 全新 STM32 版本地存储 key，避免读到旧 MSPM0 页面保存的预设。 */
        const CONFIG_STORAGE_KEY = "stm32TrackingDebugger.configuration.v1";
        const CONFIG_FORMAT = "stm32-tracking-debugger-config";
        const CONFIG_VERSION = 2;
        const PID_CHART_CHANNEL_NAME = "tracking-debugger-pid-chart-v1";
        /* 短字段与页面/图表使用的语义名称在此保持唯一映射，模式值 M 不再压缩为布尔量。 */
        const PID_CHART_ALIAS_KEYS = {
            telemetry: { R: "RUN", M: "DRIVE_MODE", AH: "ANGLE_HEADING", AE: "ANGLE_ERROR", AO: "ANGLE_OUTPUT", AS: "ANGLE_STATE", AR: "ANGLE_READY", S: "SENS", E: "ERR", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", EC: "ENCODER_CLOSED", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE" },
            imu: { AX: "ACCEL_X", AY: "ACCEL_Y", AZ: "ACCEL_Z", GX: "GYRO_X", GY: "GYRO_Y", GZ: "GYRO_Z", TEMP: "TEMPERATURE" },
            state: { R: "RUN", M: "DRIVE_MODE", SP: "SPEED", L: "LIMIT", AKP: "ANGLE_KP", AKI: "ANGLE_KI", AKD: "ANGLE_KD", AT: "ANGLE_TARGET", AMIN: "ANGLE_MINIMUM_PWM", AMAX: "ANGLE_MAXIMUM_PWM", ATOL: "ANGLE_TOLERANCE", ASET: "ANGLE_SETTLE_TIME", AR: "ANGLE_READY", AZ: "ANGLE_ZERO_YAW", AS: "ANGLE_STATE", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE", EC: "ENCODER_CLOSED", EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT", ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT" }
        };
        const textEncoder = new TextEncoder();
        const textDecoder = new TextDecoder("utf-8");
        /* 数组下标必须与固件 AnglePID_State 的 0～5 数值完全一致。 */
        const ANGLE_STATE_NAMES = ["等待 IMU", "角度空闲", "正在转向", "到位确认", "制动保持", "IMU 故障"];
        const pendingCommands = [];

        const elements = {
            connectionChip: document.getElementById("connectionChip"),
            connectionText: document.getElementById("connectionText"),
            connectButton: document.getElementById("connectButton"),
            disconnectButton: document.getElementById("disconnectButton"),
            baudRate: document.getElementById("baudRate"),
            autoReconnect: document.getElementById("autoReconnect"),
            deviceConfigStatusBadge: document.getElementById("deviceConfigStatusBadge"),
            deviceConfigStatusText: document.getElementById("deviceConfigStatusText"),
            presetSummaryText: document.getElementById("presetSummaryText"),
            refreshDeviceConfigButton: document.getElementById("refreshDeviceConfigButton"),
            captureCurrentPresetButton: document.getElementById("captureCurrentPresetButton"),
            openPresetDialogButton: document.getElementById("openPresetDialogButton"),
            presetDialogBackdrop: document.getElementById("presetDialogBackdrop"),
            presetDialog: document.getElementById("presetDialog"),
            closePresetDialogButton: document.getElementById("closePresetDialogButton"),
            presetStatusBadge: document.getElementById("presetStatusBadge"),
            presetStatusText: document.getElementById("presetStatusText"),
            presetStatusDetail: document.getElementById("presetStatusDetail"),
            loadCurrentIntoPresetButton: document.getElementById("loadCurrentIntoPresetButton"),
            savePresetButton: document.getElementById("savePresetButton"),
            exportPresetButton: document.getElementById("exportPresetButton"),
            importPresetButton: document.getElementById("importPresetButton"),
            presetFileInput: document.getElementById("presetFileInput"),
            applyPresetButton: document.getElementById("applyPresetButton"),
            presetApplyProgress: document.getElementById("presetApplyProgress"),
            presetApplyProgressFill: document.getElementById("presetApplyProgressFill"),
            presetApplyProgressText: document.getElementById("presetApplyProgressText"),
            openPidChartButton: document.getElementById("openPidChartButton"),
            notice: document.getElementById("browserNotice"),
            terminal: document.getElementById("terminal"),
            logCount: document.getElementById("logCount"),
            runState: document.getElementById("runState"),
            driveModeText: document.getElementById("driveModeText"),
            modeDescription: document.getElementById("modeDescription"),
            trackingModeButton: document.getElementById("trackingModeButton"),
            straightModeButton: document.getElementById("straightModeButton"),
            /* 模式 2 和角度控制卡片的固定 DOM 引用。 */
            angleModeButton: document.getElementById("angleModeButton"),
            encoderLoopState: document.getElementById("encoderLoopState"),
            encoderOnButton: document.getElementById("encoderOnButton"),
            encoderOffButton: document.getElementById("encoderOffButton"),
            executionDescription: document.getElementById("executionDescription"),
            baseSpeedDisplay: document.getElementById("baseSpeedDisplay"),
            speedCommandFill: document.getElementById("speedCommandFill"),
            chainSpeedValue: document.getElementById("chainSpeedValue"),
            chainTargetValue: document.getElementById("chainTargetValue"),
            chainExecutionValue: document.getElementById("chainExecutionValue"),
            chainActualValue: document.getElementById("chainActualValue"),
            chainPwmValue: document.getElementById("chainPwmValue"),
            trackingPidScope: document.getElementById("trackingPidScope"),
            anglePidScope: document.getElementById("anglePidScope"),
            angleRuntimeSummary: document.getElementById("angleRuntimeSummary"),
            trackingWeightsScope: document.getElementById("trackingWeightsScope"),
            trackingRuntimeState: document.getElementById("trackingRuntimeState"),
            encoderParameterScope: document.getElementById("encoderParameterScope"),
            encoderSyncScope: document.getElementById("encoderSyncScope"),
            encoderSyncOnButton: document.getElementById("encoderSyncOnButton"),
            encoderSyncOffButton: document.getElementById("encoderSyncOffButton"),
            controlStrategy: document.getElementById("controlStrategy"),
            response: document.getElementById("lastResponse"),
            commandToastStack: document.getElementById("commandToastStack"),
            consoleTabs: document.querySelectorAll(".console-tab"),
            consolePages: document.querySelectorAll(".console-page"),
            attitudeStage: document.getElementById("attitudeStage"),
            attitudeCamera: document.getElementById("attitudeCamera"),
            attitudeModel: document.getElementById("attitudeModel"),
            attitudeStatusText: document.getElementById("attitudeStatusText"),
            attitudeReference: document.getElementById("attitudeReference"),
            attitudeRate: document.getElementById("attitudeRate"),
            topViewButton: document.getElementById("topViewButton"),
            zeroAttitudeButton: document.getElementById("zeroAttitudeButton"),
            attitudeAxisLabels: document.querySelectorAll(".model-axis-label"),
            vehicleYawStage: document.getElementById("vehicleYawStage"),
            vehicleYawState: document.getElementById("vehicleYawState"),
            vehicleYawCar: document.getElementById("vehicleYawCar"),
            vehicleYawValue: document.getElementById("vehicleYawValue"),
            vehicleYawInitial: document.getElementById("vehicleYawInitial"),
            vehicleYawCurrent: document.getElementById("vehicleYawCurrent"),
            vehicleYawTarget: document.getElementById("vehicleYawTarget"),
            vehicleYawError: document.getElementById("vehicleYawError"),
            vehicleYawOutput: document.getElementById("vehicleYawOutput"),
            vehicleYawControlState: document.getElementById("vehicleYawControlState"),
            resetVehicleYawButton: document.getElementById("resetVehicleYawButton")
        };

