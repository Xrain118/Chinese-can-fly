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
                type: "ugv-debugger:snapshot",
                timestamp: Date.now(),
                values: { ...pidChartSnapshot },
                connection: { ...pidChartConnectionState }
            }, directTarget);
        }

        function chartNumericValues(source, values) {
            const numericValues = {};
            Object.entries(values).forEach(([rawKey, rawValue]) => {
                const key = String(rawKey).trim().toUpperCase();
                if (!key) return;
                /* 短字段已有长别名时不重复发一条曲线，避免图表里同一量出现两份。 */
                const fullAlias = PID_CHART_FIELD_ALIASES[source]?.[key];
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
                type: "ugv-debugger:data",
                timestamp: Date.now(),
                source,
                values: numericValues
            });
        }

        function openPidChartPage() {
            const chartUrl = new URL("PidChart.html", window.location.href).href;
            pidChartWindow = window.open(chartUrl, "ugvPidChart");
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
                "#startButton, #stopButton, #getAllButton, #resetButton, #defaultsButton, #directModeButton, #straightModeButton, " +
                "#sendSpeedButton, #encoderOnButton, #encoderOffButton, #sendEncoderPidButton, #sendEncoderCpsButton, " +
                "#sendEncoderLimitButton, #encoderSyncOnButton, #encoderSyncOffButton, #sendEncoderSyncButton, #sendManualButton, .quick-command, #refreshDeviceConfigButton"
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
            if (!connected) {
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
                type: "ugv-debugger:connection",
                timestamp: Date.now(),
                connection: { ...pidChartConnectionState }
            });
        }

        function setRunState(running) {
            /* RUN 状态来自车端回读。 */
            const isRunning = Number(running) === 1 || String(running).toUpperCase() === "RUN";
            elements.runState.textContent = isRunning ? "RUN" : "STOP";
            elements.runState.classList.toggle("running", isRunning);
        }

        function renderControlStrategy() {
            const isStraight = currentDriveMode === 1;
            /* 控制链按当前 F407 固件的 DIRECT/STRAIGHT 模式说明输出来源。 */
            elements.controlStrategy.textContent = isStraight ? "左右轮同速分配" : "直接 PWM";
            elements.modeDescription.textContent = isStraight
                ? "直行模式：左右轮持续使用相同 SPEED 指令。"
                : "直接模式：PWM/MOVE 可独立设置左右轮；SPEED 仍会同时设置左右轮。";
            document.getElementById("speedMeaning").textContent = isStraight
                ? "SPEED 会同时写入左右轮，并在 START 后恢复。"
                : "SPEED 设置左右同值 PWM；差速请使用 PWM/MOVE。";

            elements.chainExecutionValue.textContent = encoderLoopEnabled
                ? (encoderSyncEnabled ? "速度 PI + 同步 P" : "编码器 PI 修正 PWM")
                : "直接 PWM";
            elements.executionDescription.textContent = encoderLoopEnabled
                ? "ENC ON：目标 CPS 由当前 PWM 需求和满量程 CPS 自动换算，速度 PI 只修正最终 PWM。"
                : "ENC OFF：命令值直接作为 PWM；目标 CPS 仅作换算参考。";
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
         * 同时兼容数值 0/1 与调试时可能出现的模式文本。
         */
        function setDriveMode(mode) {
            const isStraight = Number(mode) === 1 || String(mode).toUpperCase() === "STRAIGHT";
            currentDriveMode = isStraight ? 1 : 0;
            elements.driveModeText.textContent = isStraight ? "直行模式" : "直接模式";
            elements.directModeButton.classList.toggle("active", !isStraight);
            elements.straightModeButton.classList.toggle("active", isStraight);
            elements.directModeButton.setAttribute("aria-pressed", String(!isStraight));
            elements.straightModeButton.setAttribute("aria-pressed", String(isStraight));
            renderControlStrategy();
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
