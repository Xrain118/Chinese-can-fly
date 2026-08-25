        /*
         * 调试台全局状态和 DOM 引用。
         *
         * 页面没有打包器，所有 js 文件共享同一个 script 作用域；因此跨文件变量
         * 集中放在这里，后续 serial/telemetry/ui/controls 只读写这些约定好的状态。
         */
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
        /* 串口写入必须串行化；Web Serial 同一时刻只能有一个 writer 持锁。 */
        let writeChain = Promise.resolve();
        let logLineCount = 0;
        let nextCommandId = 1;
        let lastTelemetryAt = 0;
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
        const CONFIG_VERSION = 3;
        const PID_CHART_CHANNEL_NAME = "tracking-debugger-pid-chart-v1";
        /* 短字段与页面/图表使用的语义名称在此保持唯一映射，模式值 M 不再压缩为布尔量。 */
        const PID_CHART_ALIAS_KEYS = {
            telemetry: { R: "RUN", M: "DRIVE_MODE", S: "SENS", E: "ERR", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", EC: "ENCODER_CLOSED", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE" },
            state: { R: "RUN", M: "DRIVE_MODE", SP: "SPEED", L: "LIMIT", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE", EC: "ENCODER_CLOSED", EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT", ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT" }
        };
        const textEncoder = new TextEncoder();
        const textDecoder = new TextDecoder("utf-8");
        /* 等待 OK/ERR 的命令事务；PING/HEARTBEAT 走静默路径，不进入这里刷屏。 */
        const pendingCommands = [];

        /*
         * 固定 DOM 引用表。这里会在页面加载时一次性取齐，后续逻辑不再到处
         * querySelector，避免 id 改动时错误散落在多个文件。
         */
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
            consolePages: document.querySelectorAll(".console-page")
        };

