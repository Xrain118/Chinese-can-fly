        "use strict";

        const CONFIG_BAUD_RATES = [9600, 19200, 38400, 57600, 115200];
        const PRESET_FIELD_IDS = [
            "presetNameInput", "presetBaudRateSelect", "presetAutoReconnectSelect",
            "presetDriveModeSelect", "presetSpeedInput",
            "presetAngleTargetInput", "presetAngleKpInput", "presetAngleKiInput", "presetAngleKdInput",
            "presetAngleMinimumPwmInput", "presetAngleMaximumPwmInput", "presetAngleToleranceInput", "presetAngleSettleTimeInput",
            "presetKpInput", "presetKiInput", "presetKdInput", "presetLimitInput",
            "presetEncoderEnabledSelect", "presetEncoderKpInput", "presetEncoderKiInput",
            "presetEncoderFullScaleInput", "presetEncoderLimitInput",
            "presetEncoderSyncEnabledSelect", "presetEncoderSyncKpInput",
            "presetEncoderSyncToleranceInput", "presetEncoderSyncLimitInput",
            "presetWeight1", "presetWeight2", "presetWeight3", "presetWeight4",
            "presetWeight5", "presetWeight6", "presetWeight7", "presetWeight8"
        ];
        let presetAutosaveTimer = null;
        let presetFormSyncing = false;
        let presetDialogReturnFocus = null;
        let presetCaptureRequest = null;
        const PRESET_CAPTURE_TIMEOUT_MS = 7000;

        function configObject(value, path) {
            if (!value || typeof value !== "object" || Array.isArray(value)) {
                throw new Error(path + " 必须是对象");
            }
            return value;
        }

        function configNumber(value, path, minimum, maximum, integerOnly = false) {
            if (value === null || value === undefined || String(value).trim() === "") {
                throw new Error(path + " 不能为空");
            }
            const number = Number(value);
            if (!Number.isFinite(number)) throw new Error(path + " 必须是有效数字");
            if (integerOnly && !Number.isInteger(number)) throw new Error(path + " 必须是整数");
            if (number < minimum || number > maximum) {
                throw new Error(`${path} 必须位于 ${minimum}～${maximum}`);
            }
            return number;
        }

        function configBoolean(value, path) {
            if (typeof value === "boolean") return value;
            const normalized = String(value).toLowerCase();
            if (value === 1 || normalized === "1" || normalized === "true" || normalized === "on") return true;
            if (value === 0 || normalized === "0" || normalized === "false" || normalized === "off") return false;
            throw new Error(path + " 必须是布尔值");
        }

        function configEnum(value, path, allowed) {
            const normalized = String(value ?? "").toLowerCase();
            if (!allowed.includes(normalized)) {
                throw new Error(`${path} 必须是 ${allowed.join(" / ")} 之一`);
            }
            return normalized;
        }

        /**
         * 校验并规范化导入、本地存储或页面收集到的整套配置。
         * 返回对象始终是当前版本；校验失败会抛错，调用方不会部分覆盖当前预设。
         * v1 预设没有同步环字段，读取时补入关闭状态和保守默认参数。
         */
        function validateConfiguration(rawConfiguration) {
            const raw = configObject(rawConfiguration, "配置文件");
            if (raw.format !== CONFIG_FORMAT) throw new Error("不是本调试台支持的配置文件");
            const sourceVersion = Number(raw.version);
            if (sourceVersion !== 1 && sourceVersion !== CONFIG_VERSION) {
                throw new Error(`不支持配置版本 ${raw.version ?? "未知"}，当前支持 v1 / v${CONFIG_VERSION}`);
            }

            const interfaceConfig = configObject(raw.interface, "interface");
            const drive = configObject(raw.drive, "drive");
            const tracking = configObject(raw.tracking, "tracking");
            const angle = configObject(raw.angle, "angle");
            const encoder = configObject(raw.encoder, "encoder");
            const encoderSync = sourceVersion === 1 && encoder.sync === undefined
                ? { enabled: false, kp: 0.01, toleranceCps: 50, correctionLimit: 50 }
                : configObject(encoder.sync, "encoder.sync");
            const name = String(raw.name ?? "未命名配置").trim() || "未命名配置";
            if (name.length > 80) throw new Error("配置名称不能超过 80 个字符");

            if (!Array.isArray(tracking.weights) || tracking.weights.length !== 8) {
                throw new Error("tracking.weights 必须正好包含 8 个权重");
            }
            const weights = tracking.weights.map((value, index) =>
                configNumber(value, `tracking.weights[${index}]`, -10000, 10000, true)
            );
            for (let index = 1; index < weights.length; index += 1) {
                if (weights[index] <= weights[index - 1]) {
                    throw new Error("循迹权重必须从 CH1 到 CH8 严格递增");
                }
            }

            const baudRate = configNumber(interfaceConfig.baudRate, "interface.baudRate", 9600, 115200, true);
            if (!CONFIG_BAUD_RATES.includes(baudRate)) {
                throw new Error("interface.baudRate 不是页面支持的波特率");
            }
            /* 先分别校验数值范围，再检查最小/最大 PWM 的交叉约束。 */
            const angleMinimumPwm = configNumber(angle.minimumPwm, "angle.minimumPwm", 0, 1000, true);
            const angleMaximumPwm = configNumber(angle.maximumPwm, "angle.maximumPwm", 0, 1000, true);
            if (angleMinimumPwm > angleMaximumPwm) throw new Error("angle.minimumPwm 不能大于 angle.maximumPwm");

            return {
                format: CONFIG_FORMAT,
                version: CONFIG_VERSION,
                name,
                savedAt: Number.isFinite(Date.parse(raw.savedAt)) ? new Date(raw.savedAt).toISOString() : new Date().toISOString(),
                interface: {
                    baudRate,
                    autoReconnect: configBoolean(interfaceConfig.autoReconnect, "interface.autoReconnect")
                },
                drive: {
                    mode: configEnum(drive.mode, "drive.mode", ["track", "straight", "angle"]),
                    speed: configNumber(drive.speed, "drive.speed", 0, 1000, true)
                },
                tracking: {
                    kp: configNumber(tracking.kp, "tracking.kp", 0, 2),
                    ki: configNumber(tracking.ki, "tracking.ki", 0, 2),
                    kd: configNumber(tracking.kd, "tracking.kd", 0, 1),
                    correctionLimit: configNumber(tracking.correctionLimit, "tracking.correctionLimit", 0, 500, true),
                    weights
                },
                angle: {
                    target: configNumber(angle.target, "angle.target", 0, 360),
                    kp: configNumber(angle.kp, "angle.kp", 0, 20),
                    ki: configNumber(angle.ki, "angle.ki", 0, 10),
                    kd: configNumber(angle.kd, "angle.kd", 0, 10),
                    minimumPwm: angleMinimumPwm,
                    maximumPwm: angleMaximumPwm,
                    toleranceDegrees: configNumber(angle.toleranceDegrees, "angle.toleranceDegrees", 0.5, 20),
                    settleTimeMs: configNumber(angle.settleTimeMs, "angle.settleTimeMs", 50, 2000, true)
                },
                encoder: {
                    enabled: configBoolean(encoder.enabled, "encoder.enabled"),
                    kp: configNumber(encoder.kp, "encoder.kp", 0, 1),
                    ki: configNumber(encoder.ki, "encoder.ki", 0, 10),
                    fullScaleCps: configNumber(encoder.fullScaleCps, "encoder.fullScaleCps", 100, 50000, true),
                    correctionLimit: configNumber(encoder.correctionLimit, "encoder.correctionLimit", 0, 1000, true),
                    sync: {
                        enabled: configBoolean(encoderSync.enabled, "encoder.sync.enabled"),
                        kp: configNumber(encoderSync.kp, "encoder.sync.kp", 0, 1),
                        toleranceCps: configNumber(encoderSync.toleranceCps, "encoder.sync.toleranceCps", 0, 50000, true),
                        correctionLimit: configNumber(encoderSync.correctionLimit, "encoder.sync.correctionLimit", 0, 1000, true)
                    }
                }
            };
        }

        function controlValue(id) {
            return document.getElementById(id).value;
        }

        function collectDeviceConfiguration(name = "小车当前配置") {
            return validateConfiguration({
                format: CONFIG_FORMAT,
                version: CONFIG_VERSION,
                name,
                savedAt: new Date().toISOString(),
                interface: {
                    baudRate: elements.baudRate.value,
                    autoReconnect: elements.autoReconnect.checked
                },
                drive: {
                    /* 模式以最近一次 S/T 帧回读为准，未连接时取当前界面选择。 */
                    mode: currentDriveMode === 2 ? "angle" : (currentDriveMode === 1 ? "straight" : "track"),
                    speed: controlValue("speedInput")
                },
                /* 从主界面读取车端最近一次 S 帧回填的角度配置，不采集运行时 AZ 零位。 */
                angle: {
                    target: controlValue("angleTargetInput"),
                    kp: controlValue("angleKpInput"),
                    ki: controlValue("angleKiInput"),
                    kd: controlValue("angleKdInput"),
                    minimumPwm: controlValue("angleMinimumPwmInput"),
                    maximumPwm: controlValue("angleMaximumPwmInput"),
                    toleranceDegrees: controlValue("angleToleranceInput"),
                    settleTimeMs: controlValue("angleSettleTimeInput")
                },
                tracking: {
                    kp: controlValue("kpInput"),
                    ki: controlValue("kiInput"),
                    kd: controlValue("kdInput"),
                    correctionLimit: controlValue("limitInput"),
                    weights: Array.from({ length: 8 }, (_, index) => controlValue("weight" + (index + 1)))
                },
                encoder: {
                    enabled: encoderLoopEnabled,
                    kp: controlValue("encoderKpInput"),
                    ki: controlValue("encoderKiInput"),
                    fullScaleCps: controlValue("encoderFullScaleInput"),
                    correctionLimit: controlValue("encoderLimitInput"),
                    sync: {
                        enabled: encoderSyncEnabled,
                        kp: controlValue("encoderSyncKpInput"),
                        toleranceCps: controlValue("encoderSyncToleranceInput"),
                        correctionLimit: controlValue("encoderSyncLimitInput")
                    }
                }
            });
        }

        function collectPresetConfiguration() {
            return validateConfiguration({
                format: CONFIG_FORMAT,
                version: CONFIG_VERSION,
                name: controlValue("presetNameInput"),
                savedAt: new Date().toISOString(),
                interface: {
                    baudRate: controlValue("presetBaudRateSelect"),
                    autoReconnect: controlValue("presetAutoReconnectSelect")
                },
                drive: {
                    mode: controlValue("presetDriveModeSelect"),
                    speed: controlValue("presetSpeedInput")
                },
                /* 预设对话框保存目标和调参值；本次上电零位不属于可移植配置。 */
                angle: {
                    target: controlValue("presetAngleTargetInput"),
                    kp: controlValue("presetAngleKpInput"),
                    ki: controlValue("presetAngleKiInput"),
                    kd: controlValue("presetAngleKdInput"),
                    minimumPwm: controlValue("presetAngleMinimumPwmInput"),
                    maximumPwm: controlValue("presetAngleMaximumPwmInput"),
                    toleranceDegrees: controlValue("presetAngleToleranceInput"),
                    settleTimeMs: controlValue("presetAngleSettleTimeInput")
                },
                tracking: {
                    kp: controlValue("presetKpInput"),
                    ki: controlValue("presetKiInput"),
                    kd: controlValue("presetKdInput"),
                    correctionLimit: controlValue("presetLimitInput"),
                    weights: Array.from({ length: 8 }, (_, index) => controlValue("presetWeight" + (index + 1)))
                },
                encoder: {
                    enabled: controlValue("presetEncoderEnabledSelect"),
                    kp: controlValue("presetEncoderKpInput"),
                    ki: controlValue("presetEncoderKiInput"),
                    fullScaleCps: controlValue("presetEncoderFullScaleInput"),
                    correctionLimit: controlValue("presetEncoderLimitInput"),
                    sync: {
                        enabled: controlValue("presetEncoderSyncEnabledSelect"),
                        kp: controlValue("presetEncoderSyncKpInput"),
                        toleranceCps: controlValue("presetEncoderSyncToleranceInput"),
                        correctionLimit: controlValue("presetEncoderSyncLimitInput")
                    }
                }
            });
        }

        function setControlValue(id, value) {
            document.getElementById(id).value = value;
        }

        function populatePresetDialog(configuration) {
            const config = validateConfiguration(configuration);
            presetFormSyncing = true;
            try {
                setControlValue("presetNameInput", config.name);
                setControlValue("presetBaudRateSelect", config.interface.baudRate);
                setControlValue("presetAutoReconnectSelect", config.interface.autoReconnect ? "on" : "off");
                setControlValue("presetDriveModeSelect", config.drive.mode);
                setControlValue("presetSpeedInput", config.drive.speed);
                setControlValue("presetAngleTargetInput", config.angle.target);
                setControlValue("presetAngleKpInput", config.angle.kp);
                setControlValue("presetAngleKiInput", config.angle.ki);
                setControlValue("presetAngleKdInput", config.angle.kd);
                setControlValue("presetAngleMinimumPwmInput", config.angle.minimumPwm);
                setControlValue("presetAngleMaximumPwmInput", config.angle.maximumPwm);
                setControlValue("presetAngleToleranceInput", config.angle.toleranceDegrees);
                setControlValue("presetAngleSettleTimeInput", config.angle.settleTimeMs);
                setControlValue("presetKpInput", config.tracking.kp);
                setControlValue("presetKiInput", config.tracking.ki);
                setControlValue("presetKdInput", config.tracking.kd);
                setControlValue("presetLimitInput", config.tracking.correctionLimit);
                setControlValue("presetEncoderEnabledSelect", config.encoder.enabled ? "on" : "off");
                setControlValue("presetEncoderKpInput", config.encoder.kp);
                setControlValue("presetEncoderKiInput", config.encoder.ki);
                setControlValue("presetEncoderFullScaleInput", config.encoder.fullScaleCps);
                setControlValue("presetEncoderLimitInput", config.encoder.correctionLimit);
                setControlValue("presetEncoderSyncEnabledSelect", config.encoder.sync.enabled ? "on" : "off");
                setControlValue("presetEncoderSyncKpInput", config.encoder.sync.kp);
                setControlValue("presetEncoderSyncToleranceInput", config.encoder.sync.toleranceCps);
                setControlValue("presetEncoderSyncLimitInput", config.encoder.sync.correctionLimit);
                config.tracking.weights.forEach((weight, index) => setControlValue("presetWeight" + (index + 1), weight));
            } finally {
                presetFormSyncing = false;
            }
            return config;
        }

        function formatConfigurationTime(isoText) {
            const date = new Date(isoText);
            return Number.isFinite(date.getTime()) ? date.toLocaleString("zh-CN", { hour12: false }) : "未知时间";
        }

        function setPresetStatus(state, title, detail) {
            const labels = { saved: "本机已保存", pending: "自动保存中", applying: "正在下发", error: "需要处理" };
            elements.presetStatusBadge.dataset.state = state;
            elements.presetStatusBadge.textContent = labels[state] || "预设状态";
            elements.presetStatusText.textContent = title;
            elements.presetStatusDetail.textContent = detail || "";
        }

        function updatePresetSummary(configuration, savedAt) {
            if (!configuration) {
                elements.presetSummaryText.textContent = "当前没有已保存预设。打开弹窗即可设置全部参数。";
                return;
            }
            elements.presetSummaryText.textContent = `本机预设：${configuration.name} · ${formatConfigurationTime(savedAt || configuration.savedAt)}`;
        }

        function storePresetConfiguration(configuration) {
            const normalized = validateConfiguration({ ...configuration, savedAt: new Date().toISOString() });
            const record = {
                storageVersion: 1,
                savedAt: normalized.savedAt,
                configuration: normalized
            };
            window.localStorage.setItem(CONFIG_STORAGE_KEY, JSON.stringify(record));
            currentPresetConfiguration = normalized;
            updatePresetSummary(normalized, record.savedAt);
            return record;
        }

        function readStoredPresetConfiguration() {
            const text = window.localStorage.getItem(CONFIG_STORAGE_KEY);
            if (!text) return null;
            const record = configObject(JSON.parse(text), "本机预设记录");
            const storageVersion = Number(record.storageVersion);
            if (storageVersion !== 1) throw new Error("本机预设记录版本不受支持");
            return {
                savedAt: record.savedAt,
                configuration: validateConfiguration(record.configuration)
            };
        }

        function savePresetFromDialog(title = "预设已保存到本机") {
            try {
                const configuration = collectPresetConfiguration();
                const record = storePresetConfiguration(configuration);
                setPresetStatus("saved", title, `${configuration.name} · ${formatConfigurationTime(record.savedAt)}`);
                return configuration;
            } catch (error) {
                setPresetStatus("error", "当前预设没有保存", error.message);
                return null;
            }
        }

        function schedulePresetAutosave() {
            if (presetFormSyncing || configApplyRunning) return;
            setPresetStatus("pending", "预设已修改，正在自动保存", "只有全部字段合法时才会替换上一次有效预设。");
            if (presetAutosaveTimer !== null) window.clearTimeout(presetAutosaveTimer);
            presetAutosaveTimer = window.setTimeout(() => {
                presetAutosaveTimer = null;
                savePresetFromDialog("预设修改已自动保存");
            }, 500);
        }

        function openPresetDialog(configuration = null) {
            const source = configuration || currentPresetConfiguration || collectDeviceConfiguration("新建预设");
            populatePresetDialog(source);
            presetDialogReturnFocus = document.activeElement;
            elements.presetDialogBackdrop.hidden = false;
            document.body.classList.add("preset-dialog-open");
            presetDialogOpen = true;
            elements.presetDialog.focus();
            setPresetStatus(
                currentPresetConfiguration ? "saved" : "pending",
                currentPresetConfiguration ? "已载入本机预设" : "已建立新预设",
                source.name + " · 可修改全部参数"
            );
        }

        function closePresetDialog() {
            if (!presetDialogOpen || configApplyRunning) return;
            elements.presetDialogBackdrop.hidden = true;
            document.body.classList.remove("preset-dialog-open");
            presetDialogOpen = false;
            if (presetDialogReturnFocus && typeof presetDialogReturnFocus.focus === "function") {
                presetDialogReturnFocus.focus();
            }
        }

        function vehicleSerialConnected() {
            return Boolean(serialPort && serialPort.writable && keepReading);
        }

        function finishPresetCapture(success, title, detail) {
            if (!presetCaptureRequest) return;
            window.clearTimeout(presetCaptureRequest.timeoutId);
            presetCaptureRequest = null;
            setPresetStatus(success ? "saved" : "error", title, detail);
        }

        function tryFinishPresetCapture() {
            if (!presetCaptureRequest || !vehicleConfigurationSynchronized) return;
            try {
                const timestamp = new Date().toLocaleString("zh-CN", { hour12: false }).replace(/[/:]/g, "-");
                const configuration = collectDeviceConfiguration("小车配置 " + timestamp);
                populatePresetDialog(configuration);
                const record = storePresetConfiguration(configuration);
                finishPresetCapture(true, "已把小车当前值保存为预设",
                    `${configuration.name} · ${formatConfigurationTime(record.savedAt)}`);
            } catch (error) {
                finishPresetCapture(false, "小车参数无法建立预设", error.message);
            }
        }

        async function captureDeviceConfigurationAsPreset() {
            if (!presetDialogOpen) openPresetDialog();
            if (!vehicleSerialConnected()) {
                setPresetStatus("error", "串口未连接", "请先连接小车，再点击本按钮读取 CFG 配置。");
                return;
            }

            if (presetCaptureRequest) {
                window.clearTimeout(presetCaptureRequest.timeoutId);
            }
            vehicleConfigurationSynchronized = false;
            deviceConfigurationSynchronized = false;
            const request = { timeoutId: null };
            presetCaptureRequest = request;
            request.timeoutId = window.setTimeout(() => {
                if (presetCaptureRequest !== request) return;
                finishPresetCapture(false, "读取小车参数超时", "没有收到 CFG；请检查固件版本和蓝牙串口后重试。");
            }, PRESET_CAPTURE_TIMEOUT_MS);

            setPresetStatus("applying", "正在读取小车当前值", "已发送 GET ALL；收到 CFG 后会自动建立预设。");
            const result = await sendCommand("GET ALL", true);
            if (presetCaptureRequest !== request) return;
            if (!result.success) {
                finishPresetCapture(false, "GET ALL 未被小车确认", result.message);
                return;
            }
            tryFinishPresetCapture();
        }

        function refreshDeviceConfigurationSynchronization() {
            deviceConfigurationSynchronized = vehicleConfigurationSynchronized;
            elements.captureCurrentPresetButton.disabled = false;
            elements.loadCurrentIntoPresetButton.disabled = false;
            if (deviceConfigurationSynchronized) {
                elements.deviceConfigStatusBadge.dataset.state = "saved";
                elements.deviceConfigStatusBadge.textContent = "已与小车同步";
                elements.deviceConfigStatusText.textContent = "整车 CFG 配置已回读";
            }
        }

        function markDeviceConfigurationSynchronized() {
            vehicleConfigurationSynchronized = true;
            refreshDeviceConfigurationSynchronization();
            tryFinishPresetCapture();
        }

        function markDeviceConfigurationRefreshing() {
            deviceConfigurationSynchronized = false;
            vehicleConfigurationSynchronized = false;
        }

        function markDeviceConfigurationDisconnected() {
            markDeviceConfigurationRefreshing();
            if (presetCaptureRequest) {
                finishPresetCapture(false, "读取已中止", "蓝牙串口连接已断开。");
            }
            elements.deviceConfigStatusBadge.dataset.state = "waiting";
            elements.deviceConfigStatusBadge.textContent = "等待小车回读";
            elements.deviceConfigStatusText.textContent = "连接小车后会自动发送 GET ALL；主界面随后显示实车参数";
            elements.captureCurrentPresetButton.disabled = false;
            elements.loadCurrentIntoPresetButton.disabled = false;
        }

        async function sendConfigurationCommand(command) {
            const result = await sendCommand(command, true);
            if (serialPort && serialPort.writable && keepReading) {
                markDeviceConfigurationRefreshing();
                elements.deviceConfigStatusBadge.dataset.state = "waiting";
                elements.deviceConfigStatusBadge.textContent = result.success ? "正在确认修改" : "正在恢复实车值";
                elements.deviceConfigStatusText.textContent = result.success
                    ? "小车已确认命令，正在通过 GET ALL 回读最终值"
                    : "修改未被小车接受，正在重新读取小车原值";
                sendCommand("GET ALL");
            }
            return result;
        }

        /**
         * 将一份已验证的 v2 预设转换为安全串口命令序列。
         * 先停止整车；只下发可保存配置，绝不自动恢复运行状态。
         */
        function buildConfigurationCommands(configuration) {
            const config = validateConfiguration(configuration);
            return [
                "STOP",
                "MODE " + (config.drive.mode === "straight" ? "STRAIGHT" : "DIRECT"),
                "SPEED " + config.drive.speed,
                `ENC PID ${config.encoder.kp} ${config.encoder.ki}`,
                "ENC CPS " + config.encoder.fullScaleCps,
                "ENC LIMIT " + config.encoder.correctionLimit,
                `ENC SYNC ${config.encoder.sync.kp} ${config.encoder.sync.toleranceCps} ${config.encoder.sync.correctionLimit}`,
                "ENC " + (config.encoder.enabled ? "ON" : "OFF"),
                "ENC SYNC " + (config.encoder.sync.enabled ? "ON" : "OFF")
            ];
        }

        function updatePresetProgress(completed, total, text) {
            const percent = total > 0 ? Math.round((completed / total) * 100) : 0;
            elements.presetApplyProgress.hidden = false;
            elements.presetApplyProgressFill.style.width = percent + "%";
            elements.presetApplyProgressText.textContent = `${completed}/${total} · ${text}`;
        }

        async function applyPresetBatch() {
            if (configApplyRunning) return;
            const configuration = savePresetFromDialog("预设已保存，准备下发");
            if (!configuration) return;
            if (!serialPort || !serialPort.writable || !keepReading) {
                setPresetStatus("error", "无法下发：串口未连接", "预设仍保存在本机，连接小车后可以再次下发。");
                return;
            }

            const commands = buildConfigurationCommands(configuration);
            configApplyRunning = true;
            elements.applyPresetButton.disabled = true;
            setVehicleCommandControlsDisabled(true);
            elements.closePresetDialogButton.disabled = true;
            setPresetStatus("applying", "正在安全下发全部预设", "第一条指令为 STOP；每项收到小车确认后才继续。");
            updatePresetProgress(0, commands.length, "即将发送 STOP");

            try {
                for (let index = 0; index < commands.length; index += 1) {
                    const command = commands[index];
                    updatePresetProgress(index, commands.length, "正在发送 " + command);
                    const result = await sendCommand(command, true);
                    if (!result.success) {
                        throw new Error(`${command} 未完成：${result.message}`);
                    }
                    updatePresetProgress(index + 1, commands.length, command + " 已确认");
                }

                elements.baudRate.value = String(configuration.interface.baudRate);
                elements.autoReconnect.checked = configuration.interface.autoReconnect;
                writeStoredSetting(BAUD_RATE_STORAGE_KEY, configuration.interface.baudRate);
                writeStoredSetting(AUTO_RECONNECT_STORAGE_KEY, configuration.interface.autoReconnect ? "1" : "0");
                markDeviceConfigurationRefreshing();
                elements.deviceConfigStatusBadge.dataset.state = "waiting";
                elements.deviceConfigStatusBadge.textContent = "正在回读校验";
                elements.deviceConfigStatusText.textContent = "预设下发完成，正在通过 GET ALL 刷新主界面";
                setPresetStatus("saved", `已确认 ${commands.length} 项预设`, `${configuration.name} · 小车保持 STOP，正在回读主界面`);
                sendCommand("GET ALL");
            } catch (error) {
                setPresetStatus("error", "批量下发已停止", error.message + "；小车不会自动启动。");
                appendLog("ERR", "预设批量下发停止：" + error.message, "err");
                if (serialPort && serialPort.writable && keepReading) {
                    markDeviceConfigurationRefreshing();
                    elements.deviceConfigStatusBadge.dataset.state = "waiting";
                    elements.deviceConfigStatusBadge.textContent = "正在读取部分结果";
                    elements.deviceConfigStatusText.textContent = "下发中断，正在回读小车实际保留的参数";
                    sendCommand("GET ALL");
                }
            } finally {
                configApplyRunning = false;
                elements.applyPresetButton.disabled = false;
                elements.closePresetDialogButton.disabled = false;
                setVehicleCommandControlsDisabled(!serialPort || !serialPort.writable || !keepReading);
            }
        }

        function downloadPresetJson(jsonText, fileName) {
            const link = document.createElement("a");
            let objectUrl = null;
            if (typeof URL.createObjectURL === "function") {
                objectUrl = URL.createObjectURL(new Blob([jsonText], { type: "application/json;charset=utf-8" }));
                link.href = objectUrl;
            } else {
                link.href = "data:application/json;charset=utf-8," + encodeURIComponent(jsonText);
            }
            link.download = fileName;
            link.hidden = true;
            document.body.appendChild(link);
            link.click();
            link.remove();
            if (objectUrl !== null) window.setTimeout(() => URL.revokeObjectURL(objectUrl), 0);
        }

        function presetExportFileName(now = new Date()) {
            const pad = value => String(value).padStart(2, "0");
            const timestamp = `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}_` +
                `${pad(now.getHours())}-${pad(now.getMinutes())}-${pad(now.getSeconds())}`;
            return `小车配置_${timestamp}.json`;
        }

        function exportPresetFile() {
            const configuration = savePresetFromDialog("预设已保存并准备导出");
            if (!configuration) return;
            const fileName = presetExportFileName();
            const jsonText = JSON.stringify(configuration, null, 2) + "\n";
            downloadPresetJson(jsonText, fileName);
            setPresetStatus("saved", "预设 JSON 已导出", `${fileName} 已保存到浏览器默认下载目录`);
        }

        async function importPresetFile(file) {
            if (!file) return;
            if (file.size > 1024 * 1024) {
                setPresetStatus("error", "导入失败", "配置文件不能超过 1 MB");
                return;
            }
            try {
                const configuration = validateConfiguration(JSON.parse(await file.text()));
                populatePresetDialog(configuration);
                const record = storePresetConfiguration(configuration);
                setPresetStatus("saved", "预设已完整导入并保存", `${file.name} · ${configuration.name} · ${formatConfigurationTime(record.savedAt)}`);
            } catch (error) {
                setPresetStatus("error", "导入失败，弹窗数值未改动", error.message);
            } finally {
                elements.presetFileInput.value = "";
            }
        }

        function initializeConfigurationManager() {
            PRESET_FIELD_IDS.forEach(id => {
                const control = document.getElementById(id);
                const eventName = control.tagName === "SELECT" ? "change" : "input";
                control.addEventListener(eventName, schedulePresetAutosave);
            });

            elements.openPresetDialogButton.addEventListener("click", () => openPresetDialog());
            elements.closePresetDialogButton.addEventListener("click", closePresetDialog);
            elements.presetDialogBackdrop.addEventListener("click", event => {
                if (event.target === elements.presetDialogBackdrop) closePresetDialog();
            });
            document.addEventListener("keydown", event => {
                if (event.key === "Escape" && presetDialogOpen) closePresetDialog();
            });
            elements.refreshDeviceConfigButton.addEventListener("click", () => {
                markDeviceConfigurationRefreshing();
                elements.deviceConfigStatusBadge.dataset.state = "waiting";
                elements.deviceConfigStatusBadge.textContent = "正在读取";
                elements.deviceConfigStatusText.textContent = "已发送 GET ALL，等待小车返回 CFG";
                sendCommand("GET ALL");
            });
            elements.captureCurrentPresetButton.addEventListener("click", captureDeviceConfigurationAsPreset);
            elements.loadCurrentIntoPresetButton.addEventListener("click", captureDeviceConfigurationAsPreset);
            elements.savePresetButton.addEventListener("click", () => savePresetFromDialog());
            elements.exportPresetButton.addEventListener("click", exportPresetFile);
            elements.importPresetButton.addEventListener("click", () => elements.presetFileInput.click());
            elements.presetFileInput.addEventListener("change", () => importPresetFile(elements.presetFileInput.files[0]));
            elements.applyPresetButton.addEventListener("click", applyPresetBatch);

            markDeviceConfigurationDisconnected();
            try {
                const record = readStoredPresetConfiguration();
                if (record) {
                    currentPresetConfiguration = record.configuration;
                    updatePresetSummary(record.configuration, record.savedAt);
                    setPresetStatus("saved", "本机预设已就绪", `${record.configuration.name} · ${formatConfigurationTime(record.savedAt)}`);
                } else {
                    currentPresetConfiguration = collectDeviceConfiguration("默认调试配置");
                    const saved = storePresetConfiguration(currentPresetConfiguration);
                    setPresetStatus("saved", "已建立默认本机预设", `${currentPresetConfiguration.name} · ${formatConfigurationTime(saved.savedAt)}`);
                }
            } catch (error) {
                currentPresetConfiguration = collectDeviceConfiguration("默认调试配置");
                updatePresetSummary(null);
                setPresetStatus("error", "本机预设无法读取", error.message);
            }
        }
