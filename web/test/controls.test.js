"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const controlsSource = fs.readFileSync(
    path.join(__dirname, "..", "js", "controls.js"),
    "utf8"
);

function createButton(value = "") {
    const listeners = {};
    return {
        value,
        textContent: "",
        checked: true,
        disabled: false,
        previousElementSibling: { textContent: "字段" },
        addEventListener(type, listener) {
            listeners[type] = listener;
        },
        dispatch(type) {
            return listeners[type]?.({ preventDefault() {} });
        },
        focus() {},
        replaceChildren() {},
        listeners
    };
}

function createContext(sendCommand) {
    const nodes = new Map();
    const node = (id, value = "") => {
        if (!nodes.has(id)) nodes.set(id, createButton(value));
        return nodes.get(id);
    };
    node("speedInput", "400");
    node("startButton").textContent = "启动运行";

    const elements = {
        consoleTabs: [],
        connectButton: node("connectButton"),
        disconnectButton: node("disconnectButton"),
        openPidChartButton: node("openPidChartButton"),
        baudRate: node("baudRate", "9600"),
        autoReconnect: node("autoReconnect"),
        directModeButton: node("directModeButton"),
        straightModeButton: node("straightModeButton"),
        terminal: node("terminal"),
        logCount: node("logCount")
    };

    const context = vm.createContext({
        elements,
        document: {
            getElementById: id => node(id),
            querySelectorAll: () => []
        },
        navigator: { serial: { getPorts: async () => [] } },
        serialPort: { writable: {} },
        keepReading: true,
        configApplyRunning: false,
        logLineCount: 0,
        BAUD_RATE_STORAGE_KEY: "baud",
        AUTO_RECONNECT_STORAGE_KEY: "reconnect",
        sendCommand,
        sendConfigurationCommand: async () => ({ success: true }),
        connectSerial() {},
        disconnectSerial() {},
        openPidChartPage() {},
        writeStoredSetting() {},
        clearReconnectTimer() {},
        attemptAutoReconnect() {},
        setConnectionState() {},
        setBaseSpeed() {},
        appendLog() {},
        showConsolePage() {},
        preferredPort: null,
        manualDisconnect: false,
        reconnectAttempt: 0,
        reconnectTimer: null
    });
    vm.runInContext(controlsSource, context, { filename: "controls.js" });
    return { context, nodes };
}

test("启动按钮按 SPEED 回执后再发送 START", async () => {
    const commands = [];
    const { nodes } = createContext(async command => {
        commands.push(command);
        return { success: true };
    });

    await nodes.get("startButton").dispatch("click");
    assert.deepEqual(commands, ["SPEED 400", "START", "GET ALL"]);
});

test("SPEED 失败时不发送 START", async () => {
    const commands = [];
    const { nodes } = createContext(async command => {
        commands.push(command);
        return { success: false };
    });

    await nodes.get("startButton").dispatch("click");
    assert.deepEqual(commands, ["SPEED 400"]);
});

test("等待 SPEED 回执期间点击 STOP 会取消启动", async () => {
    const commands = [];
    let finishSpeed;
    const speedCompletion = new Promise(resolve => {
        finishSpeed = resolve;
    });
    const { nodes } = createContext(command => {
        commands.push(command);
        if (command === "SPEED 400") return speedCompletion;
        return Promise.resolve({ success: true });
    });

    const starting = nodes.get("startButton").dispatch("click");
    nodes.get("stopButton").dispatch("click");
    finishSpeed({ success: true });
    await starting;

    assert.deepEqual(commands, ["SPEED 400", "STOP"]);
});
