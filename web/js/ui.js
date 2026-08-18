        /*
         * 页面渲染和命令反馈。
         *
         * 本文件不直接读串口，只根据 state/telemetry/serial 提供的状态刷新 UI。
         * 命令 toast 也是一种事务视图：发送、等待 OK/ERR、超时或断链都会在这里结算。
         */
        "use strict";

        function timeText() {
            const now = new Date();
            return now.toLocaleTimeString("zh-CN", { hour12: false }) + "." + String(now.getMilliseconds()).padStart(3, "0");
        }

        function appendLog(direction, text, type = direction.toLowerCase()) {
            /* 串口终端只保留最近 MAX_LOG_LINES 行，长时间试车不会拖慢 DOM。 */
            const line = document.createElement("div");
            line.className = "log-line " + type;

            const time = document.createElement("span");
            time.className = "log-time";
            time.textContent = timeText();

            const dir = document.createElement("span");
            dir.className = "log-dir";
            dir.textContent = direction;

            const body = document.createElement("span");
            body.className = "log-text";
            body.textContent = text;

            line.append(time, dir, body);
            elements.terminal.appendChild(line);
            logLineCount += 1;

            while (elements.terminal.children.length > MAX_LOG_LINES) {
                elements.terminal.firstElementChild.remove();
                logLineCount -= 1;
            }

            elements.logCount.textContent = logLineCount + " 行";
            elements.terminal.scrollTop = elements.terminal.scrollHeight;
        }

        function emitPidChartMessage(message, directTarget = null) {
            /* 图表窗口可能是 window.open，也可能靠 BroadcastChannel；两条路都尽量通知。 */
            if (directTarget && typeof directTarget.postMessage === "function") {
                try { directTarget.postMessage(message, "*"); } catch (error) { /* 页面可能刚刚关闭。 */ }
            }
            if (pidChartWindow && !pidChartWindow.closed && pidChartWindow !== directTarget) {
                try { pidChartWindow.postMessage(message, "*"); } catch (error) { /* 不影响串口调试。 */ }
            }
            if (pidChartChannel) {
                try { pidChartChannel.postMessage(message); } catch (error) { /* file:// 下可能禁用广播。 */ }
            }
        }

        function sendPidChartSnapshot(directTarget = null) {
            emitPidChartMessage({
                type: "tracking-debugger:snapshot",
                timestamp: Date.now(),
                values: { ...pidChartSnapshot },
                connection: { ...pidChartConnectionState }
            }, directTarget);
        }

        function chartNumericValues(source, values) {
            const numericValues = {};
            Object.entries(values).forEach(([rawKey, rawValue]) => {
                const key = String(rawKey).trim().toUpperCase();
                if (!key || (source === "telemetry" && key === "SENS")) return;
                /* 短字段已有长别名时不重复发一条曲线，避免图表里同一量出现两份。 */
                const fullAlias = PID_CHART_ALIAS_KEYS[source]?.[key];
                if (fullAlias && values[fullAlias] !== undefined) return;
                if (String(rawValue).trim() === "") return;
                const value = Number(rawValue);
                if (Number.isFinite(value)) numericValues[`${source}.${key}`] = value;
            });
            return numericValues;
        }

        function publishPidChartValues(source, values) {
            const numericValues = chartNumericValues(source, values);

            if (source === "telemetry") {
                /* 派生曲线只用于观察调参，不回写页面或固件。 */
                Object.assign(pidTelemetryState, numericValues);
                if (values.SENS !== undefined) {
                    const sensorBits = String(values.SENS).replace(/[^01]/g, "").padEnd(8, "0").slice(0, 8);
                    for (let index = 0; index < 8; index += 1) {
                        numericValues[`telemetry.CH${index + 1}`] = Number(sensorBits[index]);
                    }
                }

                const derivedPair = (targetKey, leftKey, rightKey, operation) => {
                    const left = pidTelemetryState[leftKey];
                    const right = pidTelemetryState[rightKey];
                    if (Number.isFinite(left) && Number.isFinite(right)) numericValues[targetKey] = operation(left, right);
                };
                derivedPair("derived.ENC_ERROR_L", "telemetry.TARGET_L", "telemetry.ENC_L", (target, actual) => target - actual);
                derivedPair("derived.ENC_ERROR_R", "telemetry.TARGET_R", "telemetry.ENC_R", (target, actual) => target - actual);
                derivedPair("derived.ENC_AVG", "telemetry.ENC_L", "telemetry.ENC_R", (left, right) => (left + right) / 2);
                derivedPair("derived.ENC_DIFF", "telemetry.ENC_L", "telemetry.ENC_R", (left, right) => left - right);
                derivedPair("derived.TARGET_DIFF", "telemetry.TARGET_L", "telemetry.TARGET_R", (left, right) => left - right);
                derivedPair("derived.ENC_DIFF_LEFT_WHEELS", "telemetry.ENC_LF", "telemetry.ENC_LR", (front, rear) => front - rear);
                derivedPair("derived.ENC_DIFF_RIGHT_WHEELS", "telemetry.ENC_RF", "telemetry.ENC_RR", (front, rear) => front - rear);
                derivedPair("derived.TARGET_AVG", "telemetry.TARGET_L", "telemetry.TARGET_R", (left, right) => (left + right) / 2);
                derivedPair("derived.PWM_AVG", "telemetry.PWM_L", "telemetry.PWM_R", (left, right) => (left + right) / 2);
                derivedPair("derived.PWM_DIFF", "telemetry.PWM_L", "telemetry.PWM_R", (left, right) => left - right);
            }

            Object.assign(pidChartSnapshot, numericValues);
            emitPidChartMessage({
                type: "tracking-debugger:data",
                timestamp: Date.now(),
                source,
                values: numericValues
            });
        }

        function openPidChartPage() {
            const chartUrl = new URL("TrackingPidChart.html", window.location.href).href;
            pidChartWindow = window.open(chartUrl, "trackingPidChart");
            if (!pidChartWindow) {
                elements.notice.textContent = "浏览器阻止了 PID 图表弹窗，请允许此页面打开新窗口。";
                elements.notice.classList.add("show");
                return;
            }
            window.setTimeout(() => sendPidChartSnapshot(pidChartWindow), 250);
            pidChartWindow.focus();
        }

        function commandNameFromText(command) {
            const parts = String(command).trim().split(/\s+/);
            const name = parts[0].toUpperCase();
            if (name === "BASE") return "SPEED";
            return name;
        }

        function trimCommandToasts() {
            while (elements.commandToastStack.children.length > MAX_COMMAND_TOASTS) {
                const settledToast = elements.commandToastStack.querySelector(".command-toast:not(.pending)");
                (settledToast || elements.commandToastStack.firstElementChild).remove();
            }
        }

        function updateCommandToast(transaction, state, title, detail) {
            if (!transaction.toast.isConnected) return;
            transaction.toast.className = "command-toast " + state;
            transaction.toast.querySelector(".command-toast-icon").textContent =
                state === "success" ? "✓" : (state === "failure" ? "×" : "");
            transaction.toast.querySelector(".command-toast-title").textContent = title;
            transaction.toast.querySelector(".command-toast-detail").textContent = detail;
            trimCommandToasts();

            if (state !== "pending") {
                const visibleFor = state === "success" ? 2800 : 5200;
                window.setTimeout(() => transaction.toast.remove(), visibleFor);
            }
        }

        function beginCommandFeedback(command) {
            /* 每条非静默命令都会生成一个事务，等待对应 OK/ERR 或超时结算。 */
            const toast = document.createElement("div");
            toast.className = "command-toast pending";
            toast.setAttribute("role", "status");

            const icon = document.createElement("span");
            icon.className = "command-toast-icon";
            icon.setAttribute("aria-hidden", "true");

            const content = document.createElement("div");
            const title = document.createElement("div");
            title.className = "command-toast-title";
            title.textContent = "正在发送指令";
            const detail = document.createElement("div");
            detail.className = "command-toast-detail";
            detail.textContent = command;
            content.append(title, detail);
            toast.append(icon, content);
            elements.commandToastStack.appendChild(toast);

            let resolveCompletion;
            const completion = new Promise(resolve => {
                resolveCompletion = resolve;
            });
            const transaction = {
                id: nextCommandId++,
                command,
                expectedName: commandNameFromText(command),
                toast,
                timer: null,
                settled: false,
                completion,
                resolveCompletion
            };
            pendingCommands.push(transaction);
            elements.response.className = "response pending";
            elements.response.textContent = "正在发送：" + command;
            trimCommandToasts();
            return transaction;
        }

        function settleCommandTransaction(transaction, success, message) {
            if (!transaction || transaction.settled) return;
            transaction.settled = true;
            if (transaction.timer !== null) window.clearTimeout(transaction.timer);
            const index = pendingCommands.indexOf(transaction);
            if (index >= 0) pendingCommands.splice(index, 1);

            const title = success ? "小车已确认" : "指令执行失败";
            const detail = transaction.command + " · " + message;
            updateCommandToast(transaction, success ? "success" : "failure", title, detail);
            elements.response.className = "response " + (success ? "ok" : "error");
            elements.response.textContent = title + "：" + detail;
            transaction.resolveCompletion({
                success: Boolean(success),
                command: transaction.command,
                message: String(message)
            });
        }

        function markCommandSent(transaction) {
            if (!transaction || transaction.settled) return;
            updateCommandToast(transaction, "pending", "已发送，等待小车确认", transaction.command);
            elements.response.className = "response pending";
            elements.response.textContent = "已发送，等待小车确认：" + transaction.command;
            transaction.timer = window.setTimeout(() => {
                settleCommandTransaction(transaction, false, "等待回复超时");
            }, COMMAND_TIMEOUT_MS);
        }

        function settlePendingCommand(success, line, responseCommandName = "") {
            const expected = String(responseCommandName).toUpperCase();
            /* 优先按 C 字段匹配；旧固件若没回 C，则退化为最早 pending 命令。 */
            let transaction = expected
                ? pendingCommands.find(item => item.expectedName === expected)
                : null;
            if (!expected && !transaction) transaction = pendingCommands[0];
            if (!transaction) return false;
            settleCommandTransaction(transaction, success, line);
            return true;
        }

        function failAllPendingCommands(reason) {
            [...pendingCommands].forEach(transaction => {
                settleCommandTransaction(transaction, false, reason);
            });
        }

        function setVehicleCommandControlsDisabled(disabled) {
            const baseDisabled = Boolean(disabled);
            document.querySelectorAll(
                "#startButton, #stopButton, #getAllButton, #resetButton, #defaultsButton, #trackingModeButton, #straightModeButton, #angleModeButton, " +
                "#sendAngleTargetButton, #sendAnglePidButton, #sendAnglePwmButton, #sendAngleSettlingButton, #resetVehicleYawButton, " +
                "#sendPidButton, #sendSpeedButton, #sendLimitButton, #encoderOnButton, #encoderOffButton, " +
                "#sendEncoderPidButton, #sendEncoderCpsButton, #sendEncoderLimitButton, #encoderSyncOnButton, #encoderSyncOffButton, #sendEncoderSyncButton, #sendAllWeightsButton, " +
                ".send-weight, #sendManualButton, .quick-command, #refreshDeviceConfigButton"
            ).forEach(button => {
                button.disabled = baseDisabled;
            });
        }

        function setConnectionState(connected, message, reconnecting = false) {
            /* 连接态会联动所有车端命令按钮，避免离线时继续排队危险命令。 */
            elements.connectionChip.classList.toggle("connected", connected);
            elements.connectionChip.classList.toggle("reconnecting", !connected && reconnecting);
            elements.connectionText.textContent = message || (connected ? "已连接" : "未连接");
            elements.connectButton.disabled = connected || !("serial" in navigator);
            elements.disconnectButton.disabled = !connected;
            elements.baudRate.disabled = connected;
            setVehicleCommandControlsDisabled(!connected || configApplyRunning);
            elements.resetVehicleYawButton.disabled = !connected || !angleControlReady;
            if (!connected) {
                if (typeof stopHeartbeat === "function") stopHeartbeat();
                deviceConfigurationSynchronized = false;
                if (typeof markDeviceConfigurationDisconnected === "function") markDeviceConfigurationDisconnected();
            }
            elements.captureCurrentPresetButton.disabled = false;
            elements.loadCurrentIntoPresetButton.disabled = false;
            pidChartConnectionState = {
                connected: Boolean(connected),
                message: message || (connected ? "已连接" : "未连接"),
                reconnecting: Boolean(reconnecting)
            };
            emitPidChartMessage({
                type: "tracking-debugger:connection",
                timestamp: Date.now(),
                connection: { ...pidChartConnectionState }
            });
        }

        function setRunState(running) {
            /* RUN 状态来自车端回读；只有车端确认运行后页面才开始发心跳。 */
            const isRunning = Number(running) === 1 || String(running).toUpperCase() === "RUN";
            elements.runState.textContent = isRunning ? "RUN" : "STOP";
            elements.runState.classList.toggle("running", isRunning);
            if (typeof startHeartbeat === "function" && typeof stopHeartbeat === "function") {
                if (isRunning) startHeartbeat();
                else stopHeartbeat();
            }
        }

        function renderControlStrategy() {
            const isStraight = currentDriveMode === 1;
            const isAngle = currentDriveMode === 2;
            /* 控制链按当前 F407 固件的 DIRECT/STRAIGHT 模式说明输出来源。 */
            elements.controlStrategy.textContent = isAngle
                ? "角度模式未接入"
                : (isStraight ? "左右轮同速分配" : "直接 PWM");
            elements.modeDescription.textContent = isAngle
                ? "角度模式：当前 F407 固件未接入，页面不会发送 ANGLE 命令。"
                : (isStraight
                    ? "直行模式：左右轮得到相同 SPEED 指令。"
                    : "直接模式：PWM/MOVE 直接给出左右轮目标；SPEED 不会自动分配。");
            /* 旧循迹/角度配置区保留为页面兼容占位。 */
            const trackingActive = currentDriveMode === 0;
            elements.trackingPidScope.textContent = trackingActive ? "直接模式占位" : "当前不参与输出";
            elements.trackingWeightsScope.textContent = trackingActive ? "直接模式占位" : "当前不参与输出";
            elements.trackingPidScope.classList.toggle("active", trackingActive);
            elements.trackingWeightsScope.classList.toggle("active", trackingActive);
            elements.anglePidScope.textContent = isAngle ? "角度模式生效" : "当前不参与输出";
            elements.anglePidScope.classList.toggle("active", isAngle);
            document.getElementById("speedMeaning").textContent = isAngle
                ? "角度模式未接入。"
                : (isStraight ? "直行模式下 SPEED 会同时写入左右轮。" : "直接模式下请使用 PWM/MOVE。");

            /* HOLDING 状态由车端短路制动，不能显示成仍由编码器 PI 驱动。 */
            elements.chainExecutionValue.textContent = isAngle
                ? "角度模式未接入"
                : (encoderLoopEnabled
                    ? (encoderSyncEnabled ? "速度 PI + 同步 P" : "编码器 PI 修正 PWM")
                    : "直接 PWM");
            elements.executionDescription.textContent = isAngle
                ? (encoderLoopEnabled
                    ? "ENC ON：角度模式未接入，编码器设置仍保留。"
                    : "ENC OFF：角度模式未接入。")
                : (encoderLoopEnabled
                    ? "ENC ON：目标 CPS 由当前 PWM 需求和满量程 CPS 自动换算，速度 PI 只修正最终 PWM。"
                    : "ENC OFF：命令值直接作为 PWM；目标 CPS 仅作换算参考。");
            elements.encoderParameterScope.textContent = encoderLoopEnabled ? "ENC ON 正在生效" : "ENC OFF 正在生效";
            elements.encoderParameterScope.classList.toggle("active", encoderLoopEnabled);
            renderEncoderSyncState();
        }

        function setBaseSpeed(value, confirmed = false) {
            const text = String(value ?? "").trim();
            const number = Number(value);
            if (!text || !Number.isFinite(number)) {
                elements.baseSpeedDisplay.textContent = "--";
                elements.speedCommandFill.style.width = "0%";
                return;
            }
            const clampedSpeed = Math.max(0, Math.min(1000, number));
            const displaySpeed = Number.isInteger(number) ? String(number) : number.toFixed(1);
            elements.baseSpeedDisplay.textContent = displaySpeed;
            elements.speedCommandFill.style.width = (clampedSpeed / 10) + "%";
            if (confirmed) elements.chainSpeedValue.textContent = `SPEED ${displaySpeed} / 1000`;
        }

        function renderLiveControlChain() {
            /* 控制链展示真实回读值：目标 CPS、实测 CPS、最终 PWM 分开看。 */
            const targetLeft = document.getElementById("targetLeft").textContent;
            const targetRight = document.getElementById("targetRight").textContent;
            const encoderLeft = document.getElementById("encoderLeft").textContent;
            const encoderRight = document.getElementById("encoderRight").textContent;
            const pwmLeft = document.getElementById("pwmLeft").textContent;
            const pwmRight = document.getElementById("pwmRight").textContent;
            elements.chainTargetValue.textContent = `L ${targetLeft} / R ${targetRight} CPS`;
            elements.chainActualValue.textContent = `CPS L ${encoderLeft} / R ${encoderRight}`;
            elements.chainPwmValue.textContent = `PWM L ${pwmLeft} / R ${pwmRight}`;
        }

        /**
         * 将固件状态帧中的模式值映射到页面按钮和控制链。
         * 同时兼容数值 0/1/2 与调试时可能出现的模式文本。
         */
        function setDriveMode(mode) {
            const isStraight = Number(mode) === 1 || String(mode).toUpperCase() === "STRAIGHT";
            const isAngle = Number(mode) === 2 || String(mode).toUpperCase() === "ANGLE";
            currentDriveMode = isAngle ? 2 : (isStraight ? 1 : 0);
            elements.driveModeText.textContent = isAngle ? "角度模式" : (isStraight ? "直行模式" : "循迹模式");
            elements.trackingModeButton.classList.toggle("active", !isStraight && !isAngle);
            elements.straightModeButton.classList.toggle("active", isStraight);
            elements.angleModeButton.classList.toggle("active", isAngle);
            elements.trackingModeButton.setAttribute("aria-pressed", String(!isStraight && !isAngle));
            elements.straightModeButton.setAttribute("aria-pressed", String(isStraight));
            elements.angleModeButton.setAttribute("aria-pressed", String(isAngle));
            renderControlStrategy();
        }

        /**
         * 使用 MCU 快照刷新角度摘要、方位图、状态徽标和车端归零按钮。
         * 此函数不从原始 Yaw 推导控制量，确保网页显示与实际电机控制完全同源。
         */
        function renderAngleRuntime() {
            const stateName = ANGLE_STATE_NAMES[currentAngleState] || `未知状态 ${currentAngleState}`;
            const connected = Boolean(serialPort && serialPort.writable && keepReading);
            const hasHeading = Number.isFinite(currentAngleHeading);
            const headingText = hasHeading ? currentAngleHeading.toFixed(1) : "--";
            const targetText = Number.isFinite(currentAngleTarget) ? currentAngleTarget.toFixed(1) : "--";
            const errorText = Number.isFinite(currentAngleError) ? currentAngleError.toFixed(1) : "--";

            elements.angleRuntimeSummary.textContent = `${stateName} · 当前 ${headingText}° / 目标 ${targetText}° / 误差 ${errorText}° / 输出 ${currentAngleOutput}`;
            elements.vehicleYawState.textContent = angleControlReady ? stateName : (currentAngleState === 5 ? "IMU 数据超时" : "等待车端零位");
            elements.vehicleYawState.classList.toggle("ready", angleControlReady);
            elements.vehicleYawValue.textContent = headingText;
            elements.vehicleYawTarget.textContent = targetText === "--" ? "--" : targetText + "°";
            elements.vehicleYawError.textContent = errorText === "--" ? "--" : (currentAngleError > 0 ? "+" : "") + errorText + "°";
            elements.vehicleYawOutput.textContent = String(currentAngleOutput);
            elements.vehicleYawControlState.textContent = stateName;
            elements.vehicleYawInitial.textContent = Number.isFinite(angleZeroYaw) ? angleZeroYaw.toFixed(1) + "°" : "--";
            /* 没有串口或 AR=0 时禁用车端归零，避免发送必然失败的命令序列。 */
            elements.resetVehicleYawButton.disabled = !connected || !angleControlReady;

            if (hasHeading) {
                /* CSS 变量直接使用车端罗盘角：顺时针为正，与俯视图旋转方向一致。 */
                elements.vehicleYawCar.style.setProperty("--vehicle-yaw", currentAngleHeading + "deg");
                elements.vehicleYawStage.setAttribute("aria-label", `车端相对方位角 ${headingText} 度，目标 ${targetText} 度，${stateName}`);
            }
            renderControlStrategy();
        }

        /**
         * 合并一次 S/T 帧中实际存在的角度字段。
         * S 帧通常含目标和零位，T 帧通常含航向、误差和输出，因此采用增量更新。
         */
        function setAngleRuntime(values) {
            if (values.ANGLE_HEADING !== undefined && Number.isFinite(Number(values.ANGLE_HEADING))) currentAngleHeading = Number(values.ANGLE_HEADING);
            if (values.ANGLE_TARGET !== undefined && Number.isFinite(Number(values.ANGLE_TARGET))) currentAngleTarget = Number(values.ANGLE_TARGET);
            if (values.ANGLE_ERROR !== undefined && Number.isFinite(Number(values.ANGLE_ERROR))) currentAngleError = Number(values.ANGLE_ERROR);
            if (values.ANGLE_OUTPUT !== undefined && Number.isFinite(Number(values.ANGLE_OUTPUT))) currentAngleOutput = Number(values.ANGLE_OUTPUT);
            if (values.ANGLE_STATE !== undefined && Number.isFinite(Number(values.ANGLE_STATE))) currentAngleState = Number(values.ANGLE_STATE);
            if (values.ANGLE_READY !== undefined) angleControlReady = Number(values.ANGLE_READY) === 1 || String(values.ANGLE_READY).toUpperCase() === "ON";
            if (values.ANGLE_ZERO_YAW !== undefined && Number.isFinite(Number(values.ANGLE_ZERO_YAW))) angleZeroYaw = Number(values.ANGLE_ZERO_YAW);
            renderAngleRuntime();
        }

        function setEncoderLoopState(enabled) {
            const isEnabled = Number(enabled) === 1 || String(enabled).toUpperCase() === "ON";
            encoderLoopEnabled = isEnabled;
            elements.encoderLoopState.textContent = isEnabled ? "ENC ON" : "ENC OFF";
            elements.encoderLoopState.classList.toggle("enabled", isEnabled);
            elements.encoderOnButton.classList.toggle("active", isEnabled);
            elements.encoderOffButton.classList.toggle("active", !isEnabled);
            elements.encoderOnButton.setAttribute("aria-pressed", String(isEnabled));
            elements.encoderOffButton.setAttribute("aria-pressed", String(!isEnabled));
            renderControlStrategy();
        }

        function renderEncoderSyncState() {
            if (!elements.encoderSyncScope) return;
            const ready = encoderSyncEnabled && encoderLoopEnabled;
            elements.encoderSyncScope.textContent = !encoderSyncEnabled
                ? "SYNC OFF"
                : (encoderSyncActive ? "同步修正中" : (ready ? "已开启 · 等待误差" : "等待 ENC ON"));
            elements.encoderSyncScope.classList.toggle("active", ready);
            elements.encoderSyncOnButton.classList.toggle("active", encoderSyncEnabled);
            elements.encoderSyncOffButton.classList.toggle("active", !encoderSyncEnabled);
            elements.encoderSyncOnButton.setAttribute("aria-pressed", String(encoderSyncEnabled));
            elements.encoderSyncOffButton.setAttribute("aria-pressed", String(!encoderSyncEnabled));
        }

        function setEncoderSyncState(enabled, active = encoderSyncActive) {
            encoderSyncEnabled = Number(enabled) === 1 || String(enabled).toUpperCase() === "ON";
            encoderSyncActive = Number(active) === 1 || String(active).toUpperCase() === "ON";
            renderEncoderSyncState();
            renderControlStrategy();
        }

        function setEncoderSyncActive(active) {
            encoderSyncActive = Number(active) === 1 || String(active).toUpperCase() === "ON";
            renderEncoderSyncState();
        }

        function setSignedBar(id, rawValue) {
            const value = Math.max(-1000, Math.min(1000, Number(rawValue) || 0));
            const width = Math.abs(value) / 20;
            const bar = document.getElementById(id);
            bar.style.width = width + "%";
            bar.style.left = value >= 0 ? "50%" : (50 - width) + "%";
            bar.style.background = value < 0 ? "#d08a19" : "#087f76";
        }

        function setSensorBits(rawBits) {
            const cleanBits = String(rawBits ?? "").replace(/[^01]/g, "").padEnd(8, "0").slice(0, 8);
            document.getElementById("sensorBits").textContent = cleanBits;
            document.querySelectorAll(".sensor").forEach((sensor, index) => {
                const active = cleanBits[index] === "1";
                sensor.classList.toggle("active", active);
                sensor.querySelector(".sensor-value").textContent = active ? "1" : "0";
                sensor.querySelector(".sensor-caption").textContent = active ? "黑线" : "白底";
                sensor.setAttribute("aria-label", `CH${index + 1}：${active ? "黑线" : "白底"}`);
            });
        }

        function setTrackingRuntime(values) {
            const names = ["中心", "内侧偏移", "外侧偏移", "过渡桥接", "丢线搜索"];
            const state = Number(values.TRACKING_STATE);
            const base = Number(values.TRACKING_BASE_PWM);
            const name = Number.isInteger(state) && names[state] ? names[state] : "--";
            elements.trackingRuntimeState.textContent = `${name} / ${Number.isFinite(base) ? base : "--"}`;
        }
