        "use strict";

        function parseKeyValues(payload) {
            const values = {};
            String(payload).split(",").forEach(part => {
                const item = part.trim();
                const equalAt = item.indexOf("=");
                if (equalAt <= 0) return;
                const key = item.slice(0, equalAt).trim().toUpperCase();
                const value = item.slice(equalAt + 1).trim();
                if (key) values[key] = value;
            });
            return values;
        }

        function expandKeyAliases(values, aliases) {
            const expanded = { ...values };
            Object.entries(aliases).forEach(([shortName, fullName]) => {
                if (expanded[fullName] === undefined && values[shortName] !== undefined) {
                    expanded[fullName] = values[shortName];
                }
            });
            return expanded;
        }

        function setInputValue(id, value) {
            if (value === undefined || value === "") return;
            document.getElementById(id).value = value;
        }

        function setImuNumber(id, value) {
            if (value === undefined) return;
            const number = Number(value);
            document.getElementById(id).textContent = Number.isFinite(number)
                ? (Number.isInteger(number) ? String(number) : number.toFixed(3))
                : "--";
        }

        function setAttitudeVisualState(state, text) {
            elements.attitudeStage.classList.toggle("live", state === "live");
            elements.attitudeStage.classList.toggle("stale", state === "stale");
            elements.attitudeStatusText.textContent = text;
            const statePill = document.getElementById("imuState");
            statePill.classList.toggle("ready", state === "live");
            statePill.textContent = state === "live"
                ? "原始数据"
                : (state === "stale" ? "数据超时" : "等待遥测");
        }

        function angleDelta(next, previous) {
            return ((next - previous + 540) % 360) - 180;
        }

        function trackAttitudeAngle(name, nextValue) {
            const tracker = attitudeAngles[name];
            if (tracker.raw === null) {
                tracker.raw = nextValue;
                tracker.continuous = nextValue;
                return;
            }

            tracker.continuous += angleDelta(nextValue, tracker.raw);
            tracker.raw = nextValue;
        }

        function renderAttitudeModel() {
            const roll = attitudeAngles.roll.continuous - attitudeZero.roll;
            const pitch = attitudeAngles.pitch.continuous - attitudeZero.pitch;
            const yaw = attitudeAngles.yaw.continuous - attitudeZero.yaw;
            const displayRoll = attitudeView.lockedTop ? 0 : roll;
            const displayPitch = attitudeView.lockedTop ? 0 : pitch;
            const displayYaw = yaw;

            /* 实物坐标：+X 沿模组板面向下，+Y 沿板面向左，+Z 垂直板面向上；Pitch 符号按实测方向校正。 */
            elements.attitudeModel.style.transform =
                `rotateZ(${-displayYaw}deg) rotateX(${displayPitch}deg) rotateY(${displayRoll}deg)`;
            elements.attitudeAxisLabels.forEach(label => {
                label.style.transform =
                    `rotateY(${-displayRoll}deg) rotateX(${-displayPitch}deg) rotateZ(${displayYaw}deg) ` +
                    `rotateZ(${-attitudeView.yaw}deg) rotateX(${-attitudeView.pitch}deg)`;
            });
            elements.attitudeStage.setAttribute(
                "aria-label",
                (attitudeHasData
                    ? `IMU 模组实时姿态：横滚 ${roll.toFixed(1)} 度，俯仰 ${pitch.toFixed(1)} 度，偏航 ${yaw.toFixed(1)} 度`
                    : "等待姿态传感器数据") +
                    (attitudeView.lockedTop ? "；当前为固定俯视" : "；可用鼠标或触摸拖动改变观察视角")
            );
        }

        function renderAttitudeView() {
            elements.attitudeCamera.style.setProperty("--view-pitch", attitudeView.pitch + "deg");
            elements.attitudeCamera.style.setProperty("--view-yaw", attitudeView.yaw + "deg");
            renderAttitudeModel();
        }

        function wrapDegrees360(value) {
            return ((value % 360) + 360) % 360;
        }

        function wrapDegreesSigned(value) {
            return ((value + 540) % 360) - 180;
        }

        /**
         * 清除浏览器保存的 MCU 角度快照。
         * 这里只复位显示，不会发送 ANGLE ZERO，也不会改变三维模型的姿态显示零位。
         */
        function resetVehicleYawReference() {
            currentAngleHeading = null;
            currentAngleError = 0;
            currentAngleOutput = 0;
            currentAngleState = 0;
            angleControlReady = false;
            angleZeroYaw = null;
            elements.vehicleYawCar.style.setProperty("--vehicle-yaw", "0deg");
            elements.vehicleYawValue.textContent = "--";
            elements.vehicleYawInitial.textContent = "--";
            elements.vehicleYawCurrent.textContent = "--";
            elements.vehicleYawTarget.textContent = "--";
            elements.vehicleYawError.textContent = "--";
            elements.vehicleYawOutput.textContent = "--";
            elements.vehicleYawControlState.textContent = "--";
            elements.vehicleYawState.textContent = "等待车端零位";
            elements.vehicleYawState.classList.remove("ready");
            elements.vehicleYawStage.setAttribute("aria-label", "等待车端角度状态和零位");
            elements.resetVehicleYawButton.disabled = true;
            renderAngleRuntime();
        }

        function updateAttitudeVisualization(values) {
            const roll = Number(values.ROLL);
            const pitch = Number(values.PITCH);
            const yaw = Number(values.YAW);
            if (![roll, pitch, yaw].every(Number.isFinite)) return;

            trackAttitudeAngle("roll", roll);
            trackAttitudeAngle("pitch", pitch);
            trackAttitudeAngle("yaw", yaw);
            /* I 帧原始 Yaw 只用于诊断；控制方位 AH 由独立的 S/T 帧更新。 */
            elements.vehicleYawCurrent.textContent = yaw.toFixed(1) + "°";

            const now = Date.now();
            if (lastAttitudeAt) {
                const instantRate = 1000 / Math.max(1, now - lastAttitudeAt);
                attitudeRateHz = attitudeRateHz
                    ? attitudeRateHz * 0.7 + instantRate * 0.3
                    : instantRate;
                elements.attitudeRate.textContent = attitudeRateHz.toFixed(1) + " Hz";
            }
            lastAttitudeAt = now;
            attitudeHasData = true;
            elements.zeroAttitudeButton.disabled = false;
            setAttitudeVisualState("live", "实时姿态");
            renderAttitudeModel();
        }

        function applyWheelAndSyncTelemetry(values) {
            const fieldIds = {
                PWM_L: "pwmLeft",
                PWM_R: "pwmRight",
                ENC_LF: "encoderLeftFront",
                ENC_LR: "encoderLeftRear",
                ENC_RF: "encoderRightFront",
                ENC_RR: "encoderRightRear",
                ENC_L: "encoderLeft",
                ENC_R: "encoderRight",
                TARGET_L: "targetLeft",
                TARGET_R: "targetRight",
                ENC_SYNC_DIFF: "encoderSyncError",
                ENC_SYNC_PWM: "encoderSyncCorrection"
            };
            Object.entries(fieldIds).forEach(([key, id]) => {
                if (values[key] !== undefined) document.getElementById(id).textContent = values[key];
            });
            if (values.PWM_L !== undefined) setSignedBar("pwmLeftBar", values.PWM_L);
            if (values.PWM_R !== undefined) setSignedBar("pwmRightBar", values.PWM_R);
            if (values.ENC_SYNC_ACTIVE !== undefined) setEncoderSyncActive(values.ENC_SYNC_ACTIVE);

            const targetLeft = Number(document.getElementById("targetLeft").textContent);
            const targetRight = Number(document.getElementById("targetRight").textContent);
            const measuredLeft = Number(document.getElementById("encoderLeft").textContent);
            const measuredRight = Number(document.getElementById("encoderRight").textContent);
            document.getElementById("encoderTargetDiff").textContent =
                Number.isFinite(targetLeft) && Number.isFinite(targetRight) ? targetLeft - targetRight : "--";
            document.getElementById("encoderMeasuredDiff").textContent =
                Number.isFinite(measuredLeft) && Number.isFinite(measuredRight) ? measuredLeft - measuredRight : "--";
            renderLiveControlChain();
        }

        function applyTelemetry(values) {
            if (values.RUN !== undefined) setRunState(values.RUN);
            if (values.DRIVE_MODE !== undefined) setDriveMode(values.DRIVE_MODE);
            /* 旧页面仍能接收 AH/AE/AO/AS/AR；当前 F407 固件通常不发送这些角度字段。 */
            setAngleRuntime(values);
            if (values.ENCODER_CLOSED !== undefined) setEncoderLoopState(values.ENCODER_CLOSED);
            if (values.SENS !== undefined) setSensorBits(values.SENS);
            setTrackingRuntime(values);
            if (values.ERR !== undefined) document.getElementById("errorValue").textContent = values.ERR;

            applyWheelAndSyncTelemetry(values);

            lastTelemetryAt = Date.now();
            document.getElementById("telemetryAge").textContent = "刚刚";
        }

        function applyImu(values) {
            const accel = [values.ACCEL_X, values.ACCEL_Y, values.ACCEL_Z].map(Number);
            const gyro = [values.GYRO_X, values.GYRO_Y, values.GYRO_Z].map(Number);
            if (!accel.every(Number.isFinite) || !gyro.every(Number.isFinite)) return;

            setImuNumber("imuRoll", values.ACCEL_X);
            setImuNumber("imuPitch", values.ACCEL_Y);
            setImuNumber("imuYaw", values.ACCEL_Z);
            setImuNumber("imuGyroX", values.GYRO_X);
            setImuNumber("imuGyroY", values.GYRO_Y);
            setImuNumber("imuGyroZ", values.GYRO_Z);
            setImuNumber("imuTemperature", values.TEMPERATURE);
            const now = Date.now();
            if (lastAttitudeAt) {
                const instantRate = 1000 / Math.max(1, now - lastAttitudeAt);
                attitudeRateHz = attitudeRateHz
                    ? attitudeRateHz * 0.7 + instantRate * 0.3
                    : instantRate;
                elements.attitudeRate.textContent = attitudeRateHz.toFixed(1) + " Hz";
            }
            lastAttitudeAt = now;
            attitudeHasData = true;
            setAttitudeVisualState("live", "原始 IMU 数据");
        }

        function applyState(values) {
            if (values.RUN !== undefined) setRunState(values.RUN);
            if (values.DRIVE_MODE !== undefined) setDriveMode(values.DRIVE_MODE);
            /* S 帧只同步运行快照；配置同步以独立 CFG 帧为准。 */
            setAngleRuntime(values);
            setTrackingRuntime(values);
            if (values.ENCODER_CLOSED !== undefined) setEncoderLoopState(values.ENCODER_CLOSED);
            if (values.ENC_SYNC_ENABLED !== undefined) {
                setEncoderSyncState(values.ENC_SYNC_ENABLED, values.ENC_SYNC_ACTIVE ?? encoderSyncActive);
            }
            setInputValue("kpInput", values.KP);
            setInputValue("kiInput", values.KI);
            setInputValue("kdInput", values.KD);
            setInputValue("angleKpInput", values.ANGLE_KP);
            setInputValue("angleKiInput", values.ANGLE_KI);
            setInputValue("angleKdInput", values.ANGLE_KD);
            setInputValue("angleTargetInput", values.ANGLE_TARGET);
            setInputValue("angleMinimumPwmInput", values.ANGLE_MINIMUM_PWM);
            setInputValue("angleMaximumPwmInput", values.ANGLE_MAXIMUM_PWM);
            setInputValue("angleToleranceInput", values.ANGLE_TOLERANCE);
            setInputValue("angleSettleTimeInput", values.ANGLE_SETTLE_TIME);
            setInputValue("speedInput", values.SPEED);
            if (values.SPEED !== undefined) setBaseSpeed(values.SPEED, true);
            setInputValue("limitInput", values.LIMIT);
            applyWheelAndSyncTelemetry(values);
        }

        function applyConfig(values) {
            if (values.ENCODER_CLOSED !== undefined) setEncoderLoopState(values.ENCODER_CLOSED);
            if (values.ENC_SYNC_ENABLED !== undefined) {
                setEncoderSyncState(values.ENC_SYNC_ENABLED, values.ENC_SYNC_ACTIVE ?? encoderSyncActive);
            }
            setInputValue("encoderKpInput", values.ENC_KP);
            setInputValue("encoderKiInput", values.ENC_KI);
            setInputValue("encoderFullScaleInput", values.ENC_FULL_SCALE);
            setInputValue("encoderLimitInput", values.ENC_LIMIT);
            setInputValue("encoderSyncKpInput", values.ENC_SYNC_KP);
            setInputValue("encoderSyncToleranceInput", values.ENC_SYNC_TOLERANCE);
            setInputValue("encoderSyncLimitInput", values.ENC_SYNC_LIMIT);
            for (let channel = 1; channel <= 8; channel += 1) {
                setInputValue("weight" + channel, values["W" + channel]);
            }
            if (typeof markDeviceConfigurationSynchronized === "function") markDeviceConfigurationSynchronized();
        }

        function processLine(rawLine) {
            const line = rawLine.trim();
            if (!line) return;

            const firstSpace = line.indexOf(" ");
            const prefix = (firstSpace < 0 ? line : line.slice(0, firstSpace)).toUpperCase();
            const payload = firstSpace < 0 ? "" : line.slice(firstSpace + 1).trim();
            const responseValues = (prefix === "OK" || prefix === "ERR") ? parseKeyValues(payload) : {};
            const responseCommand = responseValues.CMD ?? responseValues.C;
            const silentHeartbeatResponse =
                ["PING", "HEARTBEAT"].includes(String(responseCommand ?? "").toUpperCase());

            if (!silentHeartbeatResponse) appendLog("RX", line, "rx");

            if (prefix === "T" || prefix === "TEL") {
                /* 兼容旧角度短字段：AH 航向、AE 误差、AO 输出、AS 状态、AR 就绪。 */
                const values = expandKeyAliases(parseKeyValues(payload), {
                    R: "RUN", M: "DRIVE_MODE", AH: "ANGLE_HEADING", AE: "ANGLE_ERROR", AO: "ANGLE_OUTPUT", AS: "ANGLE_STATE", AR: "ANGLE_READY", S: "SENS", E: "ERR", NB: "TRACKING_BASE_PWM", NS: "TRACKING_STATE", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R",
                    EC: "ENCODER_CLOSED", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE"
                });
                applyTelemetry(values);
                publishPidChartValues("telemetry", values);
                return;
            }
            if (prefix === "I" || prefix === "IMU") {
                const values = expandKeyAliases(parseKeyValues(payload), {
                    AX: "ACCEL_X", AY: "ACCEL_Y", AZ: "ACCEL_Z", GX: "GYRO_X", GY: "GYRO_Y", GZ: "GYRO_Z", TEMP: "TEMPERATURE"
                });
                applyImu(values);
                publishPidChartValues("imu", values);
                return;
            }
            if (prefix === "S" || prefix === "STATE") {
                /* 状态帧更新运行快照；配置字段由 CFG 处理。 */
                const values = expandKeyAliases(parseKeyValues(payload), {
                    R: "RUN", M: "DRIVE_MODE", SP: "SPEED", L: "LIMIT", NB: "TRACKING_BASE_PWM", NS: "TRACKING_STATE", EC: "ENCODER_CLOSED",
                    AKP: "ANGLE_KP", AKI: "ANGLE_KI", AKD: "ANGLE_KD", AT: "ANGLE_TARGET", AMIN: "ANGLE_MINIMUM_PWM", AMAX: "ANGLE_MAXIMUM_PWM", ATOL: "ANGLE_TOLERANCE", ASET: "ANGLE_SETTLE_TIME", AR: "ANGLE_READY", AZ: "ANGLE_ZERO_YAW", AS: "ANGLE_STATE",
                    PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE",
                    EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT", ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT"
                });
                applyState(values);
                publishPidChartValues("state", values);
                return;
            }
            if (prefix === "CFG" || prefix === "CONFIG") {
                const values = expandKeyAliases(parseKeyValues(payload), {
                    EC: "ENCODER_CLOSED", EKP: "ENC_KP", EKI: "ENC_KI", EFS: "ENC_FULL_SCALE", ECL: "ENC_LIMIT",
                    ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT",
                    WDT: "WATCHDOG_TIMEOUT_MS", BLV: "BATTERY_LOW_MV"
                });
                applyConfig(values);
                publishPidChartValues("state", values);
                const pendingGet = pendingCommands.find(item => item.expectedName === "GET");
                if (pendingGet) settleCommandTransaction(pendingGet, true, "已收到配置数据");
                return;
            }
            if (prefix === "OK") {
                if (silentHeartbeatResponse) return;
                if (!settlePendingCommand(true, line, responseCommand)) {
                    elements.response.className = "response ok";
                    elements.response.textContent = line;
                }
                return;
            }
            if (prefix === "ERR") {
                if (silentHeartbeatResponse) return;
                if (!settlePendingCommand(false, line, responseCommand)) {
                    elements.response.className = "response error";
                    elements.response.textContent = line;
                }
            }
        }

        function consumeReceivedText(text) {
            receiveBuffer += text;
            if (receiveBuffer.length > MAX_RECEIVE_BUFFER) {
                appendLog("ERR", "接收行超过缓冲区上限，已丢弃未结束数据", "err");
                receiveBuffer = "";
                return;
            }

            const lines = receiveBuffer.split(/\r\n|\n|\r/);
            receiveBuffer = lines.pop() || "";
            lines.forEach(processLine);
        }

