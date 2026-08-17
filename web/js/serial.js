        "use strict";

        function readStoredSetting(key, fallback) {
            try {
                const value = window.localStorage.getItem(key);
                return value === null ? fallback : value;
            } catch (error) {
                return fallback;
            }
        }

        function writeStoredSetting(key, value) {
            try {
                window.localStorage.setItem(key, String(value));
            } catch (error) {
                /* file:// 环境可能禁用本地存储，不影响串口功能。 */
            }
        }

        function showConsolePage(requestedPage, persist = true, scrollToTabs = true) {
            const hasRequestedPage = [...elements.consolePages].some(
                page => page.dataset.consolePage === requestedPage
            );
            const pageName = hasRequestedPage ? requestedPage : "motion";

            elements.consoleTabs.forEach(tab => {
                tab.setAttribute("aria-selected", String(tab.dataset.page === pageName));
            });
            elements.consolePages.forEach(page => {
                page.hidden = page.dataset.consolePage !== pageName;
            });

            if (persist) writeStoredSetting(CONSOLE_PAGE_STORAGE_KEY, pageName);
            if (pageName === "imu" && attitudeHasData) {
                window.requestAnimationFrame(renderAttitudeView);
            } else if (pageName === "serial") {
                window.requestAnimationFrame(() => {
                    elements.terminal.scrollTop = elements.terminal.scrollHeight;
                });
            }

            if (scrollToTabs) {
                window.requestAnimationFrame(() => {
                    document.querySelector(".console-tabs").scrollIntoView({ block: "start", behavior: "smooth" });
                });
            }
        }

        function clearReconnectTimer() {
            if (reconnectTimer !== null) {
                window.clearTimeout(reconnectTimer);
                reconnectTimer = null;
            }
        }

        function autoReconnectEnabled() {
            return elements.autoReconnect.checked;
        }

        function scheduleReconnect() {
            if (!("serial" in navigator) || manualDisconnect || !autoReconnectEnabled() ||
                serialPort || connectAttemptPromise || reconnectTimer !== null) {
                return;
            }

            const index = Math.min(reconnectAttempt, RECONNECT_DELAYS_MS.length - 1);
            const delay = RECONNECT_DELAYS_MS[index];
            reconnectAttempt += 1;
            setConnectionState(false, "等待设备重新上线", true);
            reconnectTimer = window.setTimeout(() => {
                reconnectTimer = null;
                attemptAutoReconnect();
            }, delay);
        }

        async function attemptAutoReconnect(eventPort = null) {
            if (!("serial" in navigator) || manualDisconnect || !autoReconnectEnabled() ||
                serialPort || connectAttemptPromise) {
                return false;
            }

            let port = eventPort;
            try {
                const grantedPorts = await navigator.serial.getPorts();
                if (preferredPort && grantedPorts.includes(preferredPort)) {
                    port = preferredPort;
                } else if (grantedPorts.length === 1) {
                    port = grantedPorts[0];
                } else if (port && !grantedPorts.includes(port)) {
                    port = null;
                }

                if (!port) {
                    if (grantedPorts.length > 1) {
                        setConnectionState(false, "请选择要连接的串口");
                        return false;
                    }
                    scheduleReconnect();
                    return false;
                }

                preferredPort = port;
                return await openSerialPort(port, true);
            } catch (error) {
                scheduleReconnect();
                return false;
            }
        }

        function handleConnectionLoss(port, reason) {
            const wasCurrentPort = serialPort === port;
            if (!wasCurrentPort && preferredPort !== port) return;

            keepReading = false;
            receiveBuffer = "";
            if (wasCurrentPort) serialPort = null;
            if (reader) reader.cancel().catch(() => {});
            failAllPendingCommands(reason || "设备已断开");

            if (manualDisconnect) {
                setConnectionState(false, "未连接");
                return;
            }

            if (wasCurrentPort) {
                appendLog("SYS", (reason || "设备已断开") + "，等待自动重连", "sys");
            }
            if (autoReconnectEnabled()) {
                scheduleReconnect();
            } else {
                setConnectionState(false, "设备已断开");
            }
        }

        async function readSerialLoop(port) {
            let unexpectedEnd = false;
            try {
                while (keepReading && serialPort === port && port.readable) {
                    const activeReader = port.readable.getReader();
                    reader = activeReader;
                    try {
                        while (keepReading && serialPort === port) {
                            const { value, done } = await activeReader.read();
                            if (done) {
                                unexpectedEnd = keepReading && serialPort === port;
                                break;
                            }
                            if (value) consumeReceivedText(textDecoder.decode(value, { stream: true }));
                        }
                    } catch (error) {
                        unexpectedEnd = keepReading && serialPort === port && !isDisconnecting;
                    } finally {
                        activeReader.releaseLock();
                        if (reader === activeReader) reader = null;
                    }
                    break;
                }
            } finally {
                if (unexpectedEnd && !isDisconnecting && serialPort === port) {
                    handleConnectionLoss(port, "串口读取中断");
                }
            }
        }

        async function openSerialPort(port, automatic = false) {
            if (!port) return false;
            if (serialPort === port && keepReading) return true;
            if (connectAttemptPromise) return connectAttemptPromise;

            clearReconnectTimer();
            setConnectionState(false, automatic ? "正在自动重连…" : "正在连接…", automatic);
            connectAttemptPromise = (async () => {
                const baudRate = Number(elements.baudRate.value) || 9600;
                try {
                    keepReading = false;
                    if (reader) await reader.cancel().catch(() => {});
                    if (readLoopPromise) await readLoopPromise.catch(() => {});
                    reader = null;
                    readLoopPromise = null;

                    if (port.readable || port.writable) {
                        await port.close().catch(() => {});
                    }
                    await port.open({ baudRate, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });

                    serialPort = port;
                    preferredPort = port;
                    keepReading = true;
                    isDisconnecting = false;
                    manualDisconnect = false;
                    reconnectAttempt = 0;
                    receiveBuffer = "";
                    resetVehicleYawReference();
                    setConnectionState(true, automatic ? "已自动重连" : "已连接");
                    appendLog("SYS", (automatic ? "自动重连成功：" : "串口已打开：") + baudRate + " baud，8N1", "sys");

                    const loopPromise = readSerialLoop(port);
                    readLoopPromise = loopPromise;
                    loopPromise.finally(() => {
                        if (readLoopPromise === loopPromise) readLoopPromise = null;
                    });

                    window.setTimeout(() => {
                        if (keepReading && serialPort === port) sendCommand("GET ALL");
                    }, 180);
                    return true;
                } catch (error) {
                    if (serialPort === port) serialPort = null;
                    keepReading = false;
                    receiveBuffer = "";
                    if (automatic) {
                        setConnectionState(false, "等待设备重新上线", true);
                    } else {
                        setConnectionState(false, "未连接");
                        appendLog("ERR", "连接失败：" + error.message, "err");
                    }
                    return false;
                }
            })();

            const connected = await connectAttemptPromise;
            connectAttemptPromise = null;
            if (!connected && automatic) scheduleReconnect();
            return connected;
        }

        async function connectSerial() {
            if (!("serial" in navigator)) {
                elements.notice.textContent = "当前浏览器不支持 Web Serial。请用桌面版 Chrome 或 Edge 打开本 HTML 文件。";
                elements.notice.classList.add("show");
                return;
            }

            manualDisconnect = false;
            reconnectAttempt = 0;
            clearReconnectTimer();
            try {
                const port = await navigator.serial.requestPort();
                preferredPort = port;
                await openSerialPort(port, false);
            } catch (error) {
                if (error.name !== "NotFoundError") {
                    setConnectionState(false, "未连接");
                    appendLog("ERR", "选择串口失败：" + error.message, "err");
                }
            }
        }

        async function disconnectSerial() {
            manualDisconnect = true;
            elements.autoReconnect.checked = false;
            writeStoredSetting(AUTO_RECONNECT_STORAGE_KEY, "0");
            reconnectAttempt = 0;
            clearReconnectTimer();
            isDisconnecting = true;
            keepReading = false;
            const portToClose = serialPort;
            serialPort = null;
            failAllPendingCommands("已手动断开串口");

            try {
                if (reader) await reader.cancel().catch(() => {});
                if (readLoopPromise) await readLoopPromise.catch(() => {});
                if (portToClose && (portToClose.readable || portToClose.writable)) {
                    await portToClose.close().catch(() => {});
                }
                if (portToClose) appendLog("SYS", "串口已关闭，自动重连已暂停", "sys");
            } finally {
                reader = null;
                readLoopPromise = null;
                receiveBuffer = "";
                isDisconnecting = false;
                setConnectionState(false, "未连接");
            }
        }

        function sendCommand(command, waitForConfirmation = false) {
            const cleanCommand = String(command).replace(/[\r\n]+/g, " ").trim();
            if (!cleanCommand) {
                return Promise.resolve(waitForConfirmation
                    ? { success: false, command: "", message: "空指令" }
                    : false);
            }
            const transaction = beginCommandFeedback(cleanCommand);
            if (!serialPort || !serialPort.writable || !keepReading) {
                appendLog("ERR", "请先连接蓝牙串口", "err");
                settleCommandTransaction(transaction, false, "串口未连接");
                return waitForConfirmation ? transaction.completion : Promise.resolve(false);
            }

            const commandPort = serialPort;
            writeChain = writeChain.catch(() => false).then(async () => {
                let writer = null;
                try {
                    if (!commandPort.writable || commandPort !== serialPort) {
                        settleCommandTransaction(transaction, false, "连接状态已变化，未发送");
                        return false;
                    }
                    writer = commandPort.writable.getWriter();
                    await writer.write(textEncoder.encode(cleanCommand + "\r\n"));
                    appendLog("TX", cleanCommand, "tx");
                    markCommandSent(transaction);
                    return true;
                } catch (error) {
                    settleCommandTransaction(transaction, false, "发送中断：" + error.message);
                    if (!manualDisconnect && commandPort === serialPort) {
                        handleConnectionLoss(commandPort, "串口发送中断");
                    }
                    return false;
                } finally {
                    if (writer) writer.releaseLock();
                }
            });
            return waitForConfirmation
                ? writeChain.then(() => transaction.completion)
                : writeChain;
        }

