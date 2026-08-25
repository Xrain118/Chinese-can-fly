        /*
         * 页面启动入口。
         *
         * 前面的脚本只声明状态和函数，本文件按顺序恢复本地设置、注册浏览器事件、
         * 初始化默认显示，并尝试恢复已授权串口。
         */
        "use strict";

        const savedBaudRate = readStoredSetting(BAUD_RATE_STORAGE_KEY, "9600");
        if (elements.baudRate.querySelector(`option[value="${savedBaudRate}"]`)) {
            elements.baudRate.value = savedBaudRate;
        }
        elements.autoReconnect.checked = readStoredSetting(AUTO_RECONNECT_STORAGE_KEY, "1") !== "0";
        showConsolePage(readStoredSetting(CONSOLE_PAGE_STORAGE_KEY, "motion"), false, false);

        /* PID 图表可能是独立窗口，启动时先建立跨窗口同步通道。 */
        try {
            pidChartChannel = new BroadcastChannel(PID_CHART_CHANNEL_NAME);
            pidChartChannel.addEventListener("message", event => {
                if (event.data?.type === "tracking-debugger:chart-ready") sendPidChartSnapshot();
            });
        } catch (error) {
            pidChartChannel = null;
        }
        window.addEventListener("message", event => {
            if (event.data?.type === "tracking-debugger:chart-ready") sendPidChartSnapshot(event.source);
        });

        /* Web Serial 事件只在支持的桌面浏览器里存在；不支持时页面仍可离线看预设。 */
        if ("serial" in navigator) {
            navigator.serial.addEventListener("connect", event => {
                const connectedPort = event.port || event.target;
                if (manualDisconnect || !autoReconnectEnabled() || serialPort) return;
                appendLog("SYS", "检测到串口设备重新上线", "sys");
                attemptAutoReconnect(connectedPort);
            });
            navigator.serial.addEventListener("disconnect", event => {
                const disconnectedPort = event.port || event.target;
                if (disconnectedPort === serialPort || disconnectedPort === preferredPort ||
                    (serialPort && serialPort.readable === null)) {
                    handleConnectionLoss(disconnectedPort, "设备已断电或串口已移除");
                }
            });
            window.setTimeout(restoreGrantedSerialPort, 0);
        } else {
            elements.notice.textContent = "当前浏览器不支持 Web Serial。请使用桌面版 Chrome 或 Edge 打开本 HTML 文件。";
            elements.notice.classList.add("show");
            elements.connectButton.disabled = true;
        }

        /* 离开页面时主动取消 reader 和关闭 BroadcastChannel，减少浏览器残留锁。 */
        window.addEventListener("beforeunload", () => {
            manualDisconnect = true;
            clearReconnectTimer();
            keepReading = false;
            if (reader) reader.cancel().catch(() => {});
            if (pidChartChannel) pidChartChannel.close();
        });

        /* 周期刷新“最后遥测”时间。 */
        window.setInterval(() => {
            const now = Date.now();
            const age = document.getElementById("telemetryAge");
            if (!lastTelemetryAt) {
                age.textContent = "--";
            } else {
                const seconds = Math.floor((now - lastTelemetryAt) / 1000);
                age.textContent = seconds < 2 ? "刚刚" : seconds + "s 前";
            }
        }, 1000);

        /* 首屏默认值全部从同一套渲染函数走，避免 HTML 初始文案和 JS 状态分叉。 */
        setSensorBits("00000000");
        setDriveMode(0);
        setEncoderLoopState(0);
        setEncoderSyncState(0, 0);
        setBaseSpeed(document.getElementById("speedInput").value, true);
        renderLiveControlChain();
        initializeConfigurationManager();
        setConnectionState(false, "未连接");
        appendLog("SYS", "调试台已就绪；首次授权后将自动重连，并在连接后发送 GET ALL", "sys");
