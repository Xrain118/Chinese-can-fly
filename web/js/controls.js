        /*
         * 页面控件事件绑定。
         *
         * 这里把按钮/输入转换成串口命令或本地显示操作。真正发送由 serial.js 完成，
         * 命令是否被小车接受由 telemetry.js 收到 OK/ERR 后结算。
         */
        "use strict";

        function rangedInput(id, minimum, maximum, integerOnly = false) {
            /* 浏览器端先拦截明显越界值，减少固件反复返回 ERR ARG 的试错成本。 */
            const input = document.getElementById(id);
            const number = Number(input.value);
            if (input.value.trim() === "" || !Number.isFinite(number)) {
                appendLog("ERR", input.previousElementSibling.textContent + " 不是有效数字", "err");
                input.focus();
                return null;
            }
            if (integerOnly && !Number.isInteger(number)) {
                appendLog("ERR", input.previousElementSibling.textContent + " 必须是整数", "err");
                input.focus();
                return null;
            }
            if (number < minimum || number > maximum) {
                appendLog("ERR", input.previousElementSibling.textContent + ` 必须位于 ${minimum}～${maximum}`, "err");
                input.focus();
                return null;
            }
            return number;
        }

        elements.consoleTabs.forEach((tab, index) => {
            /* 页签支持键盘左右切换，保持调参现场不一定要用鼠标。 */
            tab.addEventListener("click", () => showConsolePage(tab.dataset.page));
            tab.addEventListener("keydown", event => {
                if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
                const direction = event.key === "ArrowRight" ? 1 : -1;
                const nextIndex = (index + direction + elements.consoleTabs.length) % elements.consoleTabs.length;
                const nextTab = elements.consoleTabs[nextIndex];
                nextTab.focus();
                showConsolePage(nextTab.dataset.page);
                event.preventDefault();
            });
        });

        elements.connectButton.addEventListener("click", connectSerial);
        elements.disconnectButton.addEventListener("click", disconnectSerial);
        elements.openPidChartButton.addEventListener("click", openPidChartPage);
        elements.baudRate.addEventListener("change", () => {
            writeStoredSetting(BAUD_RATE_STORAGE_KEY, elements.baudRate.value);
        });
        elements.autoReconnect.addEventListener("change", () => {
            /* 用户重新打开自动重连时，立即尝试恢复已授权串口。 */
            writeStoredSetting(AUTO_RECONNECT_STORAGE_KEY, elements.autoReconnect.checked ? "1" : "0");
            reconnectAttempt = 0;
            clearReconnectTimer();
            if (elements.autoReconnect.checked) {
                manualDisconnect = false;
                if (!serialPort) attemptAutoReconnect();
            } else if (!serialPort) {
                setConnectionState(false, "自动重连已关闭");
            }
        });
        document.getElementById("startButton").addEventListener("click", () => sendCommand("START"));
        document.getElementById("stopButton").addEventListener("click", () => sendCommand("STOP"));
        document.getElementById("getAllButton").addEventListener("click", () => sendCommand("GET ALL"));
        document.getElementById("resetButton").addEventListener("click", () => sendCommand("RESET"));
        document.getElementById("defaultsButton").addEventListener("click", () => sendConfigurationCommand("DEFAULTS"));
        elements.directModeButton.addEventListener("click", () => sendConfigurationCommand("MODE DIRECT"));
        elements.straightModeButton.addEventListener("click", () => sendConfigurationCommand("MODE STRAIGHT"));

        document.getElementById("sendSpeedButton").addEventListener("click", () => {
            const speed = rangedInput("speedInput", 0, 1000, true);
            if (speed !== null) sendConfigurationCommand("SPEED " + speed);
        });
        document.getElementById("speedInput").addEventListener("input", event => setBaseSpeed(event.target.value));

        document.getElementById("encoderOnButton").addEventListener("click", () => sendConfigurationCommand("ENC ON"));
        document.getElementById("encoderOffButton").addEventListener("click", () => sendConfigurationCommand("ENC OFF"));

        document.getElementById("sendEncoderPidButton").addEventListener("click", () => {
            const kp = rangedInput("encoderKpInput", 0, 1);
            const ki = rangedInput("encoderKiInput", 0, 10);
            if (kp !== null && ki !== null) sendConfigurationCommand(`ENC PID ${kp} ${ki}`);
        });

        document.getElementById("sendEncoderCpsButton").addEventListener("click", () => {
            const cps = rangedInput("encoderFullScaleInput", 100, 50000, true);
            if (cps !== null) sendConfigurationCommand("ENC CPS " + cps);
        });

        document.getElementById("sendEncoderLimitButton").addEventListener("click", () => {
            const limit = rangedInput("encoderLimitInput", 0, 1000, true);
            if (limit !== null) sendConfigurationCommand("ENC LIMIT " + limit);
        });

        document.getElementById("encoderSyncOnButton").addEventListener("click", () => sendConfigurationCommand("ENC SYNC ON"));
        document.getElementById("encoderSyncOffButton").addEventListener("click", () => sendConfigurationCommand("ENC SYNC OFF"));
        document.getElementById("sendEncoderSyncButton").addEventListener("click", () => {
            const kp = rangedInput("encoderSyncKpInput", 0, 1);
            const tolerance = rangedInput("encoderSyncToleranceInput", 0, 50000, true);
            const limit = rangedInput("encoderSyncLimitInput", 0, 1000, true);
            if (kp !== null && tolerance !== null && limit !== null) {
                sendConfigurationCommand(`ENC SYNC ${kp} ${tolerance} ${limit}`);
            }
        });

        const manualCommand = document.getElementById("manualCommand");
        document.getElementById("sendManualButton").addEventListener("click", () => {
            if (manualCommand.value.trim()) {
                sendCommand(manualCommand.value);
                manualCommand.value = "";
                manualCommand.focus();
            }
        });
        manualCommand.addEventListener("keydown", event => {
            if (event.key === "Enter") {
                event.preventDefault();
                document.getElementById("sendManualButton").click();
            }
        });

        document.querySelectorAll(".quick-command").forEach(button => {
            button.addEventListener("click", () => sendCommand(button.dataset.command));
        });

        document.getElementById("clearLogButton").addEventListener("click", () => {
            elements.terminal.replaceChildren();
            logLineCount = 0;
            elements.logCount.textContent = "0 行";
        });

        async function restoreGrantedSerialPort() {
            /* 页面刚打开时只能自动恢复唯一已授权串口；多个授权口必须让用户确认。 */
            if (!autoReconnectEnabled()) return;
            try {
                const grantedPorts = await navigator.serial.getPorts();
                if (grantedPorts.length === 1) {
                    preferredPort = grantedPorts[0];
                    await openSerialPort(preferredPort, true);
                } else if (grantedPorts.length > 1) {
                    setConnectionState(false, "请选择要连接的串口");
                }
            } catch (error) {
                setConnectionState(false, "未连接");
            }
        }

