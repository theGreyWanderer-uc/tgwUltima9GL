import { test } from "node:test";
import assert from "node:assert/strict";
import { parseDashboardView } from "./u9gl-telemetry-contract.ts";
import { dashboardScaled, dashboardFieldText } from "./u9gl-field-format.ts";
import { parseLogPage, filterLogEvents } from "./u9gl-log.ts";
import { unpackWords, fitWindow, heatLevel } from "./u9gl-heatmap-data.ts";
import { dashboardCollectedTrend, dashboardPublicationTrend, dashboardGroupStale, dashboardFeedState, dashboardSessionKey } from "./u9gl-dashboard-state.ts";

const identity = { pid: 42, processCreatedFiletime: "133000000000000001", sessionId: "0".repeat(32), producerKind: "Synthetic", producerVersion: "test" };
const point = (index, elapsedMs, memoryElapsedMs, gapBefore = false) => ({
  sampleIndex: String(index), elapsedMs, gapBefore,
  collectedMs: memoryElapsedMs === null ? { frames: elapsedMs } : { frames: elapsedMs, memory: memoryElapsedMs },
  values: memoryElapsedMs === null ? { "frames.drawCallsLast": "0" }
    : { "memory.privateBytes": "18446744073709551615", "memory.workingSet": "0", "frames.drawCallsLast": "0" },
});
const group = (values = {}) => ({ collectedQpc: "1", elapsedMs: 1, ageMs: 1900, quality: "ready", staleMs: 2000, values });
const sample = () => ({ identity, qpcFrequency: "1000", sessionStartQpc: "1", sampleIndex: "1", sampleQpc: "2", sampleUtcFiletime: null,
  elapsedMs: 1, ageMs: 0, contractMinor: 0, requestedGroups: ["frames", "memory"], skippedGroups: 1, labels: { "world.map": "Britannia [map 9]" },
  groups: { frames: group({ frameNo: "1" }), future: group({ anyField: "18446744073709551615" }) } });
const view = () => ({ version: 2, state: "live", detail: "test", current: sample(), sessions: [{ identity, history: [point(1, 1, 1)], longHistory: [point(1, 1, 1)] }],
  limits: { sessionLimit: 3, samplesPerSession: 600, historySeconds: 120, historyByteLimit: 2097152, longSamplesPerSession: 360, longHistorySeconds: 1800, longIntervalSeconds: 5 },
  historySeries: ["memory.privateBytes", "memory.workingSet", "frames.drawCallsLast"],
  schema: { pacing: [{ name: "fpsMilli", kind: "unsigned", scale: 1000, unit: "fps", names: null }],
    world: [{ name: "avatarX", kind: "signed", scale: 1, unit: "units", names: null }] },
  game: { executable: "u9-laa.exe", largeAddressAware: true, addressSpaceBytes: "4294967296" }, build: "0123456789abcdef", log: null, arrays: {} });

test("browser contract preserves exact integers and refuses already-rounded JSON numbers", () => {
  assert.equal(parseDashboardView(JSON.stringify(view())).sessions[0].history[0].values["memory.privateBytes"], "18446744073709551615");
  const rounded = view(); rounded.sessions[0].history[0].values["memory.privateBytes"] = Number.MAX_SAFE_INTEGER + 1;
  assert.throws(() => parseDashboardView(JSON.stringify(rounded)), /history/);
  const incompatible = view(); incompatible.version = 1;
  assert.throws(() => parseDashboardView(JSON.stringify(incompatible)), /contract/);
  const invalid = view(); invalid.sessions[0].history[0].elapsedMs = Infinity;
  assert.throws(() => parseDashboardView(JSON.stringify(invalid)), /history/);
});

test("groups are accepted by name, so a new group needs no contract change, but names and values stay checked", () => {
  assert.equal(parseDashboardView(JSON.stringify(view())).current.groups.future.values.anyField, "18446744073709551615");
  const badName = view(); badName.current.groups["no spaces"] = group();
  assert.throws(() => parseDashboardView(JSON.stringify(badName)), /contract/);
  const badValue = view(); badValue.current.groups.frames.values.frameNo = 1;
  assert.throws(() => parseDashboardView(JSON.stringify(badValue)), /contract/);
  const badSeries = view(); badSeries.historySeries = ["memory"];
  assert.throws(() => parseDashboardView(JSON.stringify(badSeries)), /contract/);
});

test("collected series deduplicate collection timestamps and preserve skipped-publication gaps", () => {
  const history = [point(1, 10, 0), point(2, 20, 0), point(4, 40, 0, true), point(5, 50, 50)];
  const result = dashboardCollectedTrend(history, "memory.privateBytes");
  assert.equal(result.length, 3);
  assert.equal(result[1].value, null);
  assert.equal(result[2].timeMs, 50);
  assert.equal(dashboardCollectedTrend([point(1, 10, 0)], "memory.workingSet")[0].value, 0);
  assert.equal(dashboardCollectedTrend([{ ...point(1, 10, 0), values: { "memory.privateBytes": "2000000" } }], "memory.privateBytes", 1e-6)[0].value, 2);
});

test("absent groups introduce a gap and sampled zero draw counts remain zero", () => {
  assert.equal(dashboardCollectedTrend([point(1, 10, 10), point(2, 20, null), point(3, 30, 30)], "memory.privateBytes")[1].value, null);
  assert.deepEqual(dashboardPublicationTrend([point(1, 10, 10)], "frames.drawCallsLast"), [{ timeMs: 10, value: 0 }]);
  assert.deepEqual(dashboardPublicationTrend([point(1, 10, 10)], "frames.missing"), [{ timeMs: 10, value: null }]);
});

test("fresh publications do not refresh old groups and browser loss retains stale readings", () => {
  assert.equal(dashboardGroupStale(group(), 200, "live", "connected"), true);
  assert.equal(dashboardGroupStale({ ...group(), staleMs: 10000 }, 200, "live", "connected"), false);
  assert.equal(dashboardGroupStale({ ...group(), staleMs: 10000, ageMs: 9900 }, 200, "live", "connected"), true);
  assert.equal(dashboardGroupStale({ ...group(), ageMs: 0 }, 0, "live", "reconnecting"), true);
  const current = { ...view(), current: { ageMs: 100 } };
  assert.equal(dashboardFeedState(current, "connected", 2000), "stale");
  assert.equal(dashboardFeedState(current, "reconnecting", 1), "reconnecting");
  assert.equal(dashboardFeedState({ ...current, state: "processExited" }, "connected", 0), "disconnected");
  assert.notEqual(dashboardSessionKey(identity), dashboardSessionKey({ ...identity, processCreatedFiletime: "133000000000000002" }));
});

test("signed values and field metadata are accepted; malformed ones are not", () => {
  const signed = view(); signed.current.groups.frames.values.frameNo = "-5";
  assert.equal(parseDashboardView(JSON.stringify(signed)).current.groups.frames.values.frameNo, "-5");
  for (const bad of ["-0", "-9223372036854775809", "18446744073709551616", "1.5"]) {
    const v = view(); v.current.groups.frames.values.frameNo = bad;
    assert.throws(() => parseDashboardView(JSON.stringify(v)), /contract/, bad);
  }
  const badKind = view(); badKind.schema.pacing[0].kind = "float";
  assert.throws(() => parseDashboardView(JSON.stringify(badKind)), /contract/);
  const noSchema = view(); delete noSchema.schema;
  assert.throws(() => parseDashboardView(JSON.stringify(noSchema)), /contract/);
});

test("fixed-point fields scale exactly and enum or flag fields read as words", () => {
  assert.equal(dashboardScaled("59987", 1000), "59.987");
  assert.equal(dashboardScaled("60000", 1000), "60");
  assert.equal(dashboardScaled("-1536250", 1000), "-1536.25");
  assert.equal(dashboardScaled("5", 1000), "0.005");
  assert.equal(dashboardScaled("18446744073709551615", 1000), "18446744073709551.615");
  assert.equal(dashboardScaled("42", 1), "42");
  const mip = { name: "mipMode", kind: "enum", scale: 1, unit: "", names: ["off", "nearest", "trilinear"] };
  assert.equal(dashboardFieldText(mip, "2"), "trilinear");
  assert.equal(dashboardFieldText(mip, "7"), "unknown (7)");
  assert.equal(dashboardFieldText({ ...mip, kind: "flag", names: null }, "0"), "no");
});

const logEvent = (seq, level = "info", tag = "CFG", text = "loaded", detail = null) =>
  ({ seq, receivedUtc: "2026-11-03T20:00:00.0000000Z", runPid: 42, source: "log", tag, level, text, detail });
const logPage = events => ({ events, next: events.at(-1)?.seq ?? 0, oldest: 1,
  summary: { runPid: 42, runVersion: "0.5.3", faults: 1, warnings: 1, lastSeq: 3, logFile: "u9log.txt", logBytes: 10, logGrewAgoMs: 500, crashReport: null } });

test("log pages are validated and events must rise in sequence", () => {
  assert.equal(parseLogPage(JSON.stringify(logPage([logEvent(1), logEvent(2)]))).events.length, 2);
  assert.throws(() => parseLogPage(JSON.stringify(logPage([logEvent(2), logEvent(1)]))), /order/);
  assert.throws(() => parseLogPage(JSON.stringify(logPage([{ ...logEvent(1), level: "debug" }]))), /invalid/);
  assert.throws(() => parseLogPage(JSON.stringify(logPage([{ ...logEvent(1), text: "x".repeat(513) }]))), /invalid/);
  const withLog = view(); withLog.log = logPage([]).summary;
  assert.equal(parseDashboardView(JSON.stringify(withLog)).log.faults, 1);
});

test("log filters: level is a floor, tag exact, text searches details too", () => {
  const events = [logEvent(1), logEvent(2, "warning", "GLPOST", "shader failed"), logEvent(3, "fault", "FAULT", "ACCESS_VIOLATION", "at 0x005C3FA0")];
  assert.equal(filterLogEvents(events, "", "warning", "").length, 2);
  assert.equal(filterLogEvents(events, "", "fault", "").length, 1);
  assert.equal(filterLogEvents(events, "", "all", "GLPOST").length, 1);
  assert.equal(filterLogEvents(events, "5c3fa0", "all", "")[0].seq, 3);
});

test("packed cells unpack by the layout's bit widths, exactly", () => {
  const parts = [{ name: "x", bits: 8 }, { name: "y", bits: 8 }, { name: "objects", bits: 16 }, { name: "drawn", bits: 12 }, { name: "moving", bits: 12 }, { name: "flags", bits: 8 }];
  const word = String(63n | 41n << 8n | 212n << 16n | 180n << 32n | 3n << 44n | 7n << 56n);
  assert.deepEqual(unpackWords([word], parts)[0], { x: 63, y: 41, objects: 212, drawn: 180, moving: 3, flags: 7 });
  const withArrays = view(); withArrays.arrays = { areaCells: { name: "cells", countField: "count", capacity: 256, parts } };
  withArrays.current.groups.frames.array = [word];
  assert.equal(parseDashboardView(JSON.stringify(withArrays)).current.groups.frames.array[0], word);
  const tooWide = view(); tooWide.arrays = { areaCells: { name: "cells", countField: "count", capacity: 256, parts: [{ name: "x", bits: 40 }, { name: "y", bits: 40 }] } };
  assert.throws(() => parseDashboardView(JSON.stringify(tooWide)), /contract/);
});

test("the heat map window fits the cells and the Avatar, stays square and inside the grid", () => {
  const cell = (x, y, objects = 10) => ({ x, y, objects, drawn: 0, moving: 0, flags: 0 });
  const grid = { w: 128, h: 128 };
  const w = fitWindow([cell(60, 40), cell(65, 44)], { x: 62, y: 42 }, grid);
  assert.ok(w.size >= 8 && w.x0 <= 59 && w.x0 + w.size - 1 >= 66 && w.y0 <= 39 && w.y0 + w.size - 1 >= 45, JSON.stringify(w));
  const edge = fitWindow([cell(0, 0), cell(127, 2)], null, grid);
  assert.equal(edge.x0, 0); assert.equal(edge.size, 128);
  const corner = fitWindow([cell(127, 127)], null, grid);
  assert.ok(corner.x0 + corner.size <= 128 && corner.y0 + corner.size <= 128);
  assert.equal(heatLevel(undefined, "slots", 0, 2045, 80, 95), 0);
  assert.equal(heatLevel(cell(1, 1, 1700), "slots", 1700, 2045, 80, 95), 4);
  assert.equal(heatLevel(cell(1, 1, 2000), "slots", 2000, 2045, 80, 95), 5);
  assert.equal(heatLevel(cell(1, 1, 50), "objects", 50, 2045, 80, 95), 5);
});
