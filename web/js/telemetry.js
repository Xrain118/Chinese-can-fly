        /*
         * STM32 上行帧解析和页面数据回填。
         *
         * 固件输出 T/S/CFG/OK/ERR 文本帧：这里负责把短字段扩展为页面语义名，
         * 再分发到 UI、预设同步和 PID 图表。这里不直接发送任何控制命令。
         */
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
                /* 如果固件同时发短字段和长字段，长字段优先，便于以后平滑扩展协议。 */
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

        function applyWheelAndSyncTelemetry(values) {
            /* 四轮 CPS、左右平均 CPS、目标 CPS 和同步修正都来自同一帧快照。 */
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
            /* T 帧是周期运行遥测：更新实时值，但不认为配置已同步。 */
            if (values.RUN !== undefined) setRunState(values.RUN);
            if (values.DRIVE_MODE !== undefined) setDriveMode(values.DRIVE_MODE);
            if (values.ENCODER_CLOSED !== undefined) setEncoderLoopState(values.ENCODER_CLOSED);
            applyWheelAndSyncTelemetry(values);

            lastTelemetryAt = Date.now();
            document.getElementById("telemetryAge").textContent = "刚刚";
        }

        function applyState(values) {
            if (values.RUN !== undefined) setRunState(values.RUN);
            if (values.DRIVE_MODE !== undefined) setDriveMode(values.DRIVE_MODE);
            /* S 帧只同步运行快照；配置同步以独立 CFG 帧为准。 */
            if (values.ENCODER_CLOSED !== undefined) setEncoderLoopState(values.ENCODER_CLOSED);
            if (values.ENC_SYNC_ENABLED !== undefined) {
                setEncoderSyncState(values.ENC_SYNC_ENABLED, values.ENC_SYNC_ACTIVE ?? encoderSyncActive);
            }
            setInputValue("speedInput", values.SPEED);
            if (values.SPEED !== undefined) setBaseSpeed(values.SPEED, true);
            applyWheelAndSyncTelemetry(values);
        }

        function applyConfig(values) {
            /* CFG 是 GET ALL 的配置完成点；收到它才允许预设捕获/回读状态结算。 */
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
            if (typeof markDeviceConfigurationSynchronized === "function") markDeviceConfigurationSynchronized();
        }

        function processLine(rawLine) {
            const line = rawLine.trim();
            if (!line) return;

            const firstSpace = line.indexOf(" ");
            const prefix = (firstSpace < 0 ? line : line.slice(0, firstSpace)).toUpperCase();
            const payload = firstSpace < 0 ? "" : line.slice(firstSpace + 1).trim();
            /* OK/ERR 的 C 字段用于匹配 pendingCommands。 */
            const responseValues = (prefix === "OK" || prefix === "ERR") ? parseKeyValues(payload) : {};
            const responseCommand = responseValues.CMD ?? responseValues.C;

            appendLog("RX", line, "rx");

            if (prefix === "T" || prefix === "TEL") {
                const values = expandKeyAliases(parseKeyValues(payload), {
                    R: "RUN", M: "DRIVE_MODE", PL: "PWM_L", PR: "PWM_R", EL: "ENC_L", ER: "ENC_R",
                    EC: "ENCODER_CLOSED", TL: "TARGET_L", TR: "TARGET_R", VLF: "ENC_LF", VLR: "ENC_LR", VRF: "ENC_RF", VRR: "ENC_RR", ED: "ENC_SYNC_DIFF", ESC: "ENC_SYNC_PWM", ESA: "ENC_SYNC_ACTIVE"
                });
                applyTelemetry(values);
                publishPidChartValues("telemetry", values);
                return;
            }
            if (prefix === "S" || prefix === "STATE") {
                /* 状态帧更新运行快照；配置字段由 CFG 处理。 */
                const values = expandKeyAliases(parseKeyValues(payload), {
                    R: "RUN", M: "DRIVE_MODE", SP: "SPEED", EC: "ENCODER_CLOSED",
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
                    ESE: "ENC_SYNC_ENABLED", ESKP: "ENC_SYNC_KP", EST: "ENC_SYNC_TOLERANCE", ESL: "ENC_SYNC_LIMIT"
                });
                applyConfig(values);
                publishPidChartValues("state", values);
                const pendingGet = pendingCommands.find(item => item.expectedName === "GET");
                if (pendingGet) settleCommandTransaction(pendingGet, true, "已收到配置数据");
                return;
            }
            if (prefix === "OK") {
                if (!settlePendingCommand(true, line, responseCommand)) {
                    elements.response.className = "response ok";
                    elements.response.textContent = line;
                }
                return;
            }
            if (prefix === "ERR") {
                if (!settlePendingCommand(false, line, responseCommand)) {
                    elements.response.className = "response error";
                    elements.response.textContent = line;
                }
            }
        }

        function consumeReceivedText(text) {
            /* 串口数据可能任意分块到达，先累积到换行再交给 processLine。 */
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

