import { chromium } from "@playwright/test";
import assert from "node:assert/strict";

const origin = process.env.U9GL_DASHBOARD_URL || "http://127.0.0.1:9999";
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
  await page.goto(origin);
  await page.getByRole("heading", { name: "U9GL diagnostics" }).waitFor();
  await page.getByText("Waiting for game", { exact: true }).waitFor();
  await page.evaluate(() => document.fonts.ready);
  assert(await page.evaluate(() => [...document.fonts].some(font => font.family.includes("Antonio") && font.status === "loaded")), "Local Antonio font must load");
  await page.keyboard.press("Tab");
  assert.equal(await page.locator(":focus").textContent(), "Skip to diagnostics");
  assert.equal(await page.locator(".lcars-time-trend details").count(), 0);
  assert((await page.locator(".u9gl-overlay-grid").textContent()).includes("Unavailable"));
  await page.screenshot({ path: "test-results/packaged-wide.png", fullPage: true });
  await page.setViewportSize({ width: 320, height: 900 });
  assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), "No page-level overflow at 320 px");
  await page.screenshot({ path: "test-results/packaged-narrow.png", fullPage: true });
  const view = await (await page.request.get(origin + "/api/v1/telemetry")).json();
  assert.equal(view.state, "waitingForGame");
  assert.equal(view.current, null);
  assert.deepEqual(errors, []);
  assert.deepEqual(external, []);
  console.log("PASS packaged launcher: embedded assets, local fonts, waiting state, keyboard entry, unavailable values, 320/1440px, no external requests or page errors.");
} finally { await browser.close(); }
