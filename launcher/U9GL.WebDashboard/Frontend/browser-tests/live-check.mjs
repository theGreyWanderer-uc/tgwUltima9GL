import { chromium } from "@playwright/test";
import { mkdir, appendFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import assert from "node:assert/strict";

const origin = process.env.U9GL_DASHBOARD_URL || "http://127.0.0.1:9999";
const output = process.env.U9GL_LIVE_REPORT_DIR;
if (!output) throw new Error("Dashboard live check: set U9GL_LIVE_REPORT_DIR to a temporary report directory.");
await mkdir(output, { recursive: true });
const browser = await chromium.launch({ channel: process.env.U9GL_BROWSER_CHANNEL || "msedge" });
let latest = null, events = 0, acceptedEvents = 0, reloads = 0, beganLive = 0, recordedLive = false;
const states = new Set(), errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => new URL(route.request().url()).origin === origin ? route.continue() : route.abort());
  await page.exposeFunction("recordDashboardEvent", async json => {
    const view = JSON.parse(json);
    latest = view;
    states.add(view.state);
    events++;
    if (view.current) acceptedEvents++;
    await appendFile(join(output, "browser-events.jsonl"), JSON.stringify({
      receivedUtc: new Date().toISOString(), state: view.state, detail: view.detail,
      current: view.current, sessions: view.sessions.map(s => ({ identity: s.identity, points: s.history.length,
        first: s.history[0]?.sampleIndex, last: s.history.at(-1)?.sampleIndex,
        gaps: s.history.filter(p => p.gapBefore).length })),
    }) + "\n");
  });
  await page.addInitScript(() => {
    const NativeEventSource = window.EventSource;
    window.EventSource = class extends NativeEventSource {
      constructor(...args) {
        super(...args);
        this.addEventListener("telemetry", event => { void window.recordDashboardEvent(event.data); });
      }
    };
  });
  await page.goto(origin);
  console.log("READY: browser recorder waiting for the game; reports: " + output);
  const deadline = Date.now() + 15 * 60 * 1000;
  let exited = false;
  while (Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 250));
    if (!latest) continue;
    if (latest.current && !beganLive) beganLive = Date.now();
    if (latest.state === "live" && latest.current?.groups.memory && !recordedLive) {
      assert.equal(latest.current.identity.producerKind, "Game");
      await page.locator("[data-feed-state='live']").waitFor({ timeout: 5000 });
      await page.screenshot({ path: join(output, "live-wide.png"), fullPage: true });
      recordedLive = true;
      console.log("LIVE: real game values and browser display captured");
    }
    if (recordedLive && reloads === 0 && Date.now() - beganLive > 8000 && latest.state === "live") {
      const before = latest;
      reloads++;
      await page.reload();
      await page.waitForFunction(() => document.querySelector("[data-feed-state='live']"), undefined, { timeout: 5000 });
      const after = await (await page.request.get(origin + "/api/v1/telemetry")).json();
      assert.equal(after.current.identity.sessionId, before.current.identity.sessionId);
      assert(BigInt(after.current.sampleIndex) >= BigInt(before.current.sampleIndex));
      assert(after.sessions.at(-1).history.length >= Math.min(before.sessions.at(-1).history.length, 600));
      console.log("RECONNECT: same session and retained history after browser reload");
    }
    if (latest.state === "processExited") {
      exited = true;
      await page.locator("[data-feed-state='disconnected']").waitFor({ timeout: 5000 });
      const final = await (await page.request.get(origin + "/api/v1/telemetry")).json();
      assert(final.current, "Game exit must retain the accepted snapshot");
      assert(final.sessions.at(-1).history.length > 0);
      await writeFile(join(output, "retained-view.json"), JSON.stringify(final, null, 2));
      await page.screenshot({ path: join(output, "retained-wide.png"), fullPage: true });
      await page.setViewportSize({ width: 320, height: 900 });
      assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
      await page.screenshot({ path: join(output, "retained-narrow.png"), fullPage: true });
      console.log("EXIT: retained readings/history and disconnected browser state captured");
      break;
    }
  }
  const summary = { events, acceptedEvents, reloads, recordedLive, exited, states: [...states], errors };
  await writeFile(join(output, "summary.json"), JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary));
  assert(exited && recordedLive && reloads === 1, "Live game, browser reconnect and retained exit must all be observed");
  assert.deepEqual(errors, []);
} finally { await browser.close(); }
