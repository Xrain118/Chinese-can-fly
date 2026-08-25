"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

/*
 * telemetry.js 在浏览器中作为普通脚本加载。这里用独立上下文执行它，只测试
 * 不依赖 DOM 的协议纯函数，防止重构字段映射时悄悄改变解析结果。
 */
const telemetrySource = fs.readFileSync(
    path.join(__dirname, "..", "js", "telemetry.js"),
    "utf8"
);
const context = vm.createContext({
    FRAME_FIELD_ALIASES: {
        telemetry: { R: "RUN", PL: "PWM_L" },
        state: { SP: "SPEED" }
    }
});
vm.runInContext(telemetrySource, context, { filename: "telemetry.js" });

function plain(value) {
    /* 去掉 vm 上下文原型，便于用严格相等断言检查字段内容。 */
    return JSON.parse(JSON.stringify(value));
}

test("键值解析保留等号后的完整内容并规范化键名", () => {
    assert.deepEqual(
        plain(context.parseKeyValues(" r=1, message=A=B, empty= ")),
        { R: "1", MESSAGE: "A=B", EMPTY: "" }
    );
});

test("长字段存在时不会被短字段别名覆盖", () => {
    assert.deepEqual(
        plain(context.expandKeyAliases({ R: "0", RUN: "1" }, { R: "RUN" })),
        { R: "0", RUN: "1" }
    );
});

test("协议帧拆分统一处理首尾空格和大小写", () => {
    assert.deepEqual(
        plain(context.splitProtocolFrame("  tel R=1,PL=-20  ")),
        { line: "tel R=1,PL=-20", prefix: "TEL", payload: "R=1,PL=-20" }
    );
});

test("帧字段使用集中别名表扩展", () => {
    assert.deepEqual(
        plain(context.parseFrameValues("R=1,PL=-20", "telemetry")),
        { R: "1", PL: "-20", RUN: "1", PWM_L: "-20" }
    );
});
