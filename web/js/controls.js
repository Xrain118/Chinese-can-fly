        "use strict";

        function rangedInput(id, minimum, maximum, integerOnly = false) {
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

        function readAndValidateWeights() {
            const weights = [];
            for (let channel = 1; channel <= 8; channel += 1) {
                const value = rangedInput("weight" + channel, -10000, 10000, true);
                if (value === null) return null;
                if (channel > 1 && value <= weights[channel - 2]) {
                    appendLog("ERR", `权重必须从 CH1 到 CH8 严格递增：CH${channel} 必须大于 CH${channel - 1}`, "err");
                    document.getElementById("weight" + channel).focus();
                    return null;
                }
                weights.push(value);
            }
            return weights;
        }

        elements.consoleTabs.forEach((tab, index) => {
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
        /* 三个按钮分别发送固件定义的模式 0/1/2；实际高亮仍等待 S 帧回读确认。 */
        elements.trackingModeButton.addEventListener("click", () => sendConfigurationCommand("MODE TRACK"));
        elements.straightModeButton.addEventListener("click", () => sendConfigurationCommand("MODE STRAIGHT"));
        elements.angleModeButton.addEventListener("click", () => sendConfigurationCommand("MODE ANGLE"));

        elements.attitudeStage.addEventListener("pointerdown", event => {
            if (attitudeView.pointerId !== null || (event.pointerType === "mouse" && event.button !== 0)) return;
            attitudeView.lockedTop = false;
            elements.topViewButton.setAttribute("aria-pressed", "false");
            attitudeView.pointerId = event.pointerId;
            attitudeView.startX = event.clientX;
            attitudeView.startY = event.clientY;
            attitudeView.startPitch = attitudeView.pitch;
            attitudeView.startYaw = attitudeView.yaw;
            elements.attitudeStage.classList.add("dragging");
            elements.attitudeStage.setPointerCapture(event.pointerId);
            event.preventDefault();
        });

        elements.attitudeStage.addEventListener("pointermove", event => {
            if (event.pointerId !== attitudeView.pointerId) return;
            const deltaX = event.clientX - attitudeView.startX;
            const deltaY = event.clientY - attitudeView.startY;
            attitudeView.yaw = attitudeView.startYaw - deltaX * 0.42;
            attitudeView.pitch = attitudeView.startPitch - deltaY * 0.42;
            renderAttitudeView();
            event.preventDefault();
        });

        function finishAttitudeViewDrag(event) {
            if (event.pointerId !== attitudeView.pointerId) return;
            const pointerId = attitudeView.pointerId;
            attitudeView.pointerId = null;
            elements.attitudeStage.classList.remove("dragging");
            if (elements.attitudeStage.hasPointerCapture(pointerId)) {
                elements.attitudeStage.releasePointerCapture(pointerId);
            }
        }

        elements.attitudeStage.addEventListener("pointerup", finishAttitudeViewDrag);
        elements.attitudeStage.addEventListener("pointercancel", finishAttitudeViewDrag);
        elements.attitudeStage.addEventListener("lostpointercapture", finishAttitudeViewDrag);

        elements.topViewButton.addEventListener("click", () => {
            attitudeView.pitch = 0;
            attitudeView.yaw = 0;
            attitudeView.lockedTop = true;
            elements.topViewButton.setAttribute("aria-pressed", "true");
            renderAttitudeView();
        });

        /*
         * 车端归零会改变控制坐标系，所以严格串行等待 STOP 成功后再发 ANGLE ZERO，
         * 最后 GET ALL 读取新目标、零位和状态。任一步失败都不继续后续命令。
         */
        elements.resetVehicleYawButton.addEventListener("click", async () => {
            const stopResult = await sendCommand("STOP", true);
            if (!stopResult.success) return;
            const zeroResult = await sendCommand("ANGLE ZERO", true);
            if (!zeroResult.success) return;
            await sendCommand("GET ALL");
        });

        /* 三维模型姿态归零是纯显示功能，不发送任何车端命令，故与上方按钮分开。 */
        elements.zeroAttitudeButton.addEventListener("click", () => {
            if (!attitudeHasData) return;

            attitudeZeroed = !attitudeZeroed;
            if (attitudeZeroed) {
                attitudeZero.roll = attitudeAngles.roll.continuous;
                attitudeZero.pitch = attitudeAngles.pitch.continuous;
                attitudeZero.yaw = attitudeAngles.yaw.continuous;
            } else {
                attitudeZero.roll = 0;
                attitudeZero.pitch = 0;
                attitudeZero.yaw = 0;
            }

            elements.zeroAttitudeButton.setAttribute("aria-pressed", String(attitudeZeroed));
            elements.zeroAttitudeButton.textContent = attitudeZeroed ? "恢复绝对姿态" : "以当前姿态归零";
            elements.attitudeReference.textContent = attitudeZeroed ? "相对零位" : "绝对姿态";
            renderAttitudeModel();
        });

        document.getElementById("sendPidButton").addEventListener("click", () => {
            const kp = rangedInput("kpInput", 0, 2);
            const ki = rangedInput("kiInput", 0, 2);
            const kd = rangedInput("kdInput", 0, 1);
            if (kp !== null && ki !== null && kd !== null) sendConfigurationCommand(`PID ${kp} ${ki} ${kd}`);
        });

        /* 以下四组输入边界与固件 AnglePID.h 完全一致，错误值在浏览器端先行拦截。 */
        document.getElementById("sendAngleTargetButton").addEventListener("click", () => {
            const target = rangedInput("angleTargetInput", 0, 360);
            if (target !== null) sendConfigurationCommand(`ANGLE TARGET ${target}`);
        });

        document.getElementById("sendAnglePidButton").addEventListener("click", () => {
            const kp = rangedInput("angleKpInput", 0, 20);
            const ki = rangedInput("angleKiInput", 0, 10);
            const kd = rangedInput("angleKdInput", 0, 10);
            if (kp !== null && ki !== null && kd !== null) sendConfigurationCommand(`ANGLE PID ${kp} ${ki} ${kd}`);
        });

        document.getElementById("sendAnglePwmButton").addEventListener("click", () => {
            const minimum = rangedInput("angleMinimumPwmInput", 0, 1000, true);
            const maximum = rangedInput("angleMaximumPwmInput", 0, 1000, true);
            if (minimum === null || maximum === null) return;
            /* 单项范围都合法后仍需检查 min<=max 这一组间约束。 */
            if (minimum > maximum) {
                appendLog("ERR", "角度最小转向 PWM 不能大于最大转向 PWM", "err");
                document.getElementById("angleMinimumPwmInput").focus();
                return;
            }
            sendConfigurationCommand(`ANGLE PWM ${minimum} ${maximum}`);
        });

        document.getElementById("sendAngleSettlingButton").addEventListener("click", () => {
            const tolerance = rangedInput("angleToleranceInput", 0.5, 20);
            const settleTime = rangedInput("angleSettleTimeInput", 50, 2000, true);
            if (tolerance !== null && settleTime !== null) sendConfigurationCommand(`ANGLE SETTLE ${tolerance} ${settleTime}`);
        });

        document.getElementById("sendSpeedButton").addEventListener("click", () => {
            const speed = rangedInput("speedInput", 0, 1000, true);
            if (speed !== null) sendConfigurationCommand("SPEED " + speed);
        });
        document.getElementById("speedInput").addEventListener("input", event => setBaseSpeed(event.target.value));

        document.getElementById("sendLimitButton").addEventListener("click", () => {
            const limit = rangedInput("limitInput", 0, 500, true);
            if (limit !== null) sendConfigurationCommand("LIMIT " + limit);
        });

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

        document.querySelectorAll(".send-weight").forEach(button => {
            button.addEventListener("click", () => {
                const channel = Number(button.dataset.channel);
                const weights = readAndValidateWeights();
                if (weights !== null) sendConfigurationCommand(`WEIGHT ${channel} ${weights[channel - 1]}`);
            });
        });

        document.getElementById("sendAllWeightsButton").addEventListener("click", () => {
            const weights = readAndValidateWeights();
            if (weights !== null) sendConfigurationCommand("WEIGHTS " + weights.join(" "));
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

