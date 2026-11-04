import { chromium } from "@playwright/test";
import assert from "node:assert/strict";
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";

const origin = process.env.U9GL_DASHBOARD_URL || "http://127.0.0.1:9999";
const output = process.env.U9GL_LIVE_REPORT_DIR;
if (!output) throw new Error("Dashboard retained check: set U9GL_LIVE_REPORT_DIR.");
await mkdir(output, { recursive: true });
const browser = await chromium.launch({ channel: process.env.U9GL_BROWSER_CHANNEL || "msedge" });
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
  const errors = [], external = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", route => {
    if (new URL(route.request().url()).origin === origin) return route.continue();
    external.push(route.request().url());
    return route.abort();
  });
  const before = await (await page.request.get(origin + "/api/v1/telemetry")).json();
  assert.equal(before.state, "processExited");
  assert.equal(before.current.identity.producerKind, "Game");
  assert(before.sessions.length > 0 && before.sessions.length <= 3);
  const session = before.sessions.at(-1);
  assert(session.history.length > 0 && session.history.length <= 600);
  for (let i = 1; i < session.history.length; i++) {
    assert(BigInt(session.history[i].sampleIndex) > BigInt(session.history[i - 1].sampleIndex));
    assert(session.history[i].elapsedMs >= session.history[i - 1].elapsedMs);
  }
  assert(session.history.at(-1).elapsedMs - session.history[0].elapsedMs <= 120000);
  assert.equal(session.history.at(-1).sampleIndex, before.current.sampleIndex);
  await writeFile(join(output, "retained-view.json"), JSON.stringify(before, null, 2));
  await page.goto(origin);
  await page.locator("[data-feed-state='disconnected']").waitFor();
  const privateReading = page.locator(".lcars-numeric-readout").filter({ has: page.locator("dt", { hasText: "Private memory" }) });
  const displayedPrivate = (Number(before.current.groups.memory.values.privateBytes) / 1048576)
    .toLocaleString("en-US", { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  assert.equal(await privateReading.locator("strong").textContent(), displayedPrivate);
  assert.equal(await privateReading.getAttribute("data-quality"), "stale");
  await page.getByRole("navigation", { name: "Dashboard navigation" }).getByRole("link", { name: "History 06" }).click();
  await page.locator(".lcars-time-trend summary").first().click();
  assert.notEqual(await page.locator(".lcars-time-trend details").first().getAttribute("open"), null);
  assert(await page.getByRole("table", { name: "Private memory (MiB) · supplied values" }).locator("tbody tr").count() > 0);
  await page.screenshot({ path: join(output, "retained-wide.png"), fullPage: true });
  await page.setViewportSize({ width: 320, height: 900 });
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1));
  await page.screenshot({ path: join(output, "retained-narrow.png"), fullPage: true });
  await page.reload();
  await page.locator("[data-feed-state='disconnected']").waitFor();
  const after = await (await page.request.get(origin + "/api/v1/telemetry")).json();
  assert.equal(after.current.identity.sessionId, before.current.identity.sessionId);
  assert.equal(after.current.sampleIndex, before.current.sampleIndex);
  assert.equal(after.sessions.at(-1).history.length, session.history.length);
  assert(after.current.ageMs > before.current.ageMs);
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  const summary = {
    passed: true, pid: before.current.identity.pid, sessionId: before.current.identity.sessionId,
    lastSample: before.current.sampleIndex, retainedPoints: session.history.length,
    firstElapsedMs: session.history[0].elapsedMs, lastElapsedMs: session.history.at(-1).elapsedMs,
    gaps: session.history.filter(p => p.gapBefore).length,
    memoryCollections: new Set(session.history.map(p => p.collectedMs.memory).filter(t => t !== undefined)).size,
    beforeAgeMs: before.current.ageMs, afterAgeMs: after.current.ageMs,
    browserReconnectPreservedHistory: true, external, errors,
  };
  await writeFile(join(output, "retained-summary.json"), JSON.stringify(summary, null, 2));
  console.log(JSON.stringify(summary));
} finally { await browser.close(); }
