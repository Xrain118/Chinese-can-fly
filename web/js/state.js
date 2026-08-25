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
        const RECONNECT_DELAYS_MS = [400, 800, 1500, 2500, 4000];
        const AUTO_RECONNECT_STORAGE_KEY = "ugvDebugger.autoReconnect";
        const BAUD_RATE_STORAGE_KEY = "ugvDebugger.baudRate.f407";
        const CONSOLE_PAGE_STORAGE_KEY = "ugvDebugger.consolePage";
        const CONFIG_STORAGE_KEY = "stm32UgvDebugger.configuration.v1";
        const CONFIG_FORMAT = "stm32-ugv-debugger-config";
        const CONFIG_VERSION = 4;
        const PID_CHART_CHANNEL_NAME = "ugv-debugger-pid-chart-v1";
        /*
         * 固件帧的短字段只在这里维护一份。遥测解析和 PID 图表共用同一张表，
         * 避免新增字段时只更新其中一处，造成主界面与图表含义不一致。
         */
        const FRAME_FIELD_ALIASES = {
            telemetry: { R: "RUN", M: "DRIVE_MODE", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", EC: "ENCODER_CLOSED", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE" },
            state: { R: "RUN", M: "DRIVE_MODE", SP: "SPEED", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE", EC: "ENCODER_CLOSED", EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT", ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT" },
            config: { EC: "ENCODER_CLOSED", EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT", ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT" }
        };
        /* 旧状态帧的 L 字段仅供图表显示，页面状态解析仍保持原有字段集合。 */
        const PID_CHART_FIELD_ALIASES = {
            ...FRAME_FIELD_ALIASES,
            state: { ...FRAME_FIELD_ALIASES.state, L: "LIMIT" }
        };
        const textEncoder = new TextEncoder();
        const textDecoder = new TextDecoder("utf-8");
        /* 等待 OK/ERR 的命令事务。 */
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
            directModeButton: document.getElementById("directModeButton"),
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

