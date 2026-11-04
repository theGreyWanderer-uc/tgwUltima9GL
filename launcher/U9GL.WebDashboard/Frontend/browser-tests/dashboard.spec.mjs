import { test, expect } from "@playwright/test";

test.beforeEach(async ({ page, baseURL }) => {
  await page.route("**/*", route => route.request().url().startsWith(baseURL) ? route.continue() : route.abort());
});

test("embedded dashboard consumes the actual native synthetic transport without external requests", async ({ page, request }) => {
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "U9GL diagnostics" })).toBeVisible();
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", /^(live|stale|disconnected)$/);
  const response = await request.get("/api/v1/telemetry");
  const view = await response.json();
  expect(view.current.identity.producerKind).toBe("Synthetic");
  expect(view.version).toBe(2);
  expect(BigInt(view.current.groups.memory.values.privateBytes)).toBeGreaterThan(0n);
  expect(view.current.skippedGroups).toBe(1);
  for (const name of ["area", "addressSpace", "vram", "overrideResident"]) expect(view.current.groups[name]).toBeTruthy();
  expect(view.sessions.at(-1).longHistory.length).toBeGreaterThan(0);
  await page.goto("/#world");
  await expect(page.locator(".lcars-numeric-readout").filter({ has: page.locator("dt", { hasText: /^Objects$/ }) }).locator("strong")).not.toHaveText("Unavailable");
  const heat = page.locator(".u9gl-heat-grid");
  await expect(heat).toBeVisible();
  const shown = await heat.locator(".u9gl-heat-cell").count();
  expect(Math.sqrt(shown) % 1).toBe(0);
  await expect(heat.locator('.u9gl-heat-cell[tabindex="0"]')).toHaveCount(30);
  await expect(heat.locator(".u9gl-heat-avatar")).toHaveCount(1);
  await heat.locator('.u9gl-heat-cell[tabindex="0"]').first().focus();
  await expect(page.locator(".u9gl-heat-readout")).toContainText("objects");
  await expect(page.getByRole("table", { name: "Busiest cells" })).toBeVisible();
  await expect(page.getByRole("table", { name: "Meshes with the most handles" }).locator("tbody tr")).toHaveCount(15);
  await page.screenshot({ path: "test-results/world-page.png", fullPage: true });
  await page.goto("/#textures");
  await expect(page.getByText("The 40 most recently used of", { exact: false })).toBeVisible();
  await expect(page.getByRole("table", { name: "Textures in use" }).locator("tbody tr")).toHaveCount(15);
  await page.screenshot({ path: "test-results/textures-page.png", fullPage: true });
  await page.goto("/");
  expect(view.sessions.at(-1).history.length).toBeLessThanOrEqual(600);
  await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption("cardassian");
  await page.getByRole("combobox", { name: "Density", exact: true }).selectOption("compact");
  await expect(page.locator(".lcars")).toHaveAttribute("data-lcars-theme", "cardassian");
  await expect(page.locator(".lcars")).toHaveAttribute("data-lcars-density", "compact");
  await page.setViewportSize({ width: 320, height: 900 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
  await page.screenshot({ path: "test-results/native-narrow.png", fullPage: true });
  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.screenshot({ path: "test-results/native-wide.png", fullPage: true });
  await expect(page.locator('a[href^="/licenses/"]')).toHaveCount(0);
  for (const file of ["meta-react-LICENSE.txt", "trekcolors-LICENSE.txt", "antonio-OFL.txt"]) {
    const notice = await request.get(`/licenses/${file}`);
    expect(notice.status()).toBe(200);
    expect(notice.headers()["content-type"]).toContain("text/plain");
    expect(await notice.text()).not.toBe("");
  }
  expect(errors).toEqual([]);
});

const identity = { pid: 42, processCreatedFiletime: "133000000000000001", sessionId: "1".repeat(32), producerKind: "Synthetic", producerVersion: "browser-replay" };
function replayView() {
  return {
    version: 2, state: "live", detail: "Browser replay", limits: { sessionLimit: 3, samplesPerSession: 600, historySeconds: 120, historyByteLimit: 2097152, longSamplesPerSession: 360, longHistorySeconds: 1800, longIntervalSeconds: 5 },
    historySeries: ["memory.privateBytes", "memory.workingSet", "frames.drawCallsLast"],
    schema: { memory: ["privateBytes", "workingSet", "peakWorkingSet", "pagefile"].map(name => ({ name, kind: "unsigned", scale: 1, unit: "bytes", names: null })) },
    game: { executable: "u9-laa.exe", largeAddressAware: true, addressSpaceBytes: "4294967296" }, build: "0123456789abcdef", log: null, arrays: {},
    current: {
      identity, qpcFrequency: "1000", sessionStartQpc: "1000", sampleIndex: "18446744073709551615", sampleQpc: "2000", sampleUtcFiletime: null, elapsedMs: 1000, ageMs: 0,
      contractMinor: 0, requestedGroups: ["frames", "resources", "memory"], skippedGroups: 0, labels: {},
      groups: {
        memory: { collectedQpc: "2000", elapsedMs: 1000, ageMs: 0, quality: "ready", staleMs: 10000, values: { privateBytes: "18446744073709551615", workingSet: "0", peakWorkingSet: "18446744073709551615", pagefile: "0" } },
      },
    },
    sessions: [{ identity, history: [{ sampleIndex: "18446744073709551615", elapsedMs: 1000, gapBefore: false, collectedMs: { memory: 1000 }, values: { "memory.privateBytes": "18446744073709551615", "memory.workingSet": "0" } }], longHistory: [] }],
  };
}

test("replay preserves exact u64, ages retained readings, and keeps rejected updates out of the view", async ({ page }) => {
  await page.addInitScript(() => {
    sessionStorage.setItem("u9gl.dashboard.reloadedAt", String(Date.now()));
    window.__dashboardSources = [];
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.__dashboardSources.push(this); }
      close() {}
    };
  });
  await page.goto("/");
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "connecting");
  const view = replayView();
  const emit = payload => page.evaluate(data => window.__dashboardSources[0].dispatchEvent(new MessageEvent("telemetry", { data: JSON.stringify(data) })), payload);
  await emit(view);
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "live");
  const privateReading = page.locator(".lcars-numeric-readout").filter({ has: page.locator("dt", { hasText: "Private memory" }) });
  await expect(privateReading.locator("dt")).toHaveText("Private memory (MiB)");
  await expect(privateReading.locator("strong")).toHaveText("17,592,186,044,416.00");
  await expect(page.locator("#overview-tile-1 .lcars-system-status")).toHaveAttribute("data-condition", "critical");
  await page.evaluate(() => window.__dashboardSources[0].onerror(new Event("error")));
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "reconnecting");
  await expect(privateReading).toContainText("Stale");
  await emit({ ...view, version: 99 });
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "error");
  await expect(privateReading.locator("strong")).toHaveText("17,592,186,044,416.00");
  await emit({ ...view, state: "processExited", detail: "Game process exited" });
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "disconnected");
  await expect(page.getByText("Retained snapshot", { exact: true })).toBeVisible();
  await page.getByRole("navigation", { name: "Dashboard navigation" }).getByRole("link", { name: "Exact values 08" }).click();
  await page.getByRole("button", { name: /Exact value/ }).click();
  await page.getByRole("searchbox", { name: "Filter Telemetry metrics" }).fill("Private memory");
  await expect(page.getByRole("table")).toContainText("18,446,744,073,709,551,615");
  await page.getByLabel("Select Private memory").check();
  await expect(page.getByLabel("Select Private memory")).toBeChecked();
  await page.setViewportSize({ width: 320, height: 900 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.screenshot({ path: "test-results/replay-exact-values.png", fullPage: true });
});

test("subsystem pages replace content while preserving the feed, appearance and browser history", async ({ page }) => {
  await page.addInitScript(() => {
    window.__dashboardSources = [];
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.__dashboardSources.push(this); }
      close() {}
    };
  });
  await page.goto("/");
  await page.waitForFunction(() => window.__dashboardSources.length === 1);
  await page.evaluate(view => window.__dashboardSources[0].dispatchEvent(new MessageEvent("telemetry", { data: JSON.stringify(view) })), replayView());
  const navigation = page.getByRole("navigation", { name: "Dashboard navigation" });
  await expect(page.getByRole("heading", { name: "Overview", exact: true })).toBeVisible();
  await expect(page.locator(".u9gl-tile")).toHaveCount(10);
  await expect(page.getByText("u9-laa.exe · large address aware, 4,096 MiB address space")).toBeVisible();
  await expect(page.getByRole("heading", { name: "Session history" })).toHaveCount(0);
  await expect(page.getByRole("table")).toHaveCount(0);
  await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption("voyager");
  await navigation.getByRole("link", { name: "Memory 03" }).click();
  await expect(page).toHaveURL(/#memory$/);
  await expect(page.getByRole("heading", { name: "Leak indicator", exact: true })).toBeVisible();
  await expect(page.locator(".lcars-numeric-readout").filter({ has: page.locator("dt", { hasText: /^Working set/ }) }).locator("strong")).toHaveText("0.00");
  await expect(page.locator(".u9gl-capacity .lcars-gauge")).toHaveAttribute("aria-valuenow", "100");
  await navigation.getByRole("link", { name: "World 05" }).click();
  await expect(page.getByRole("heading", { name: "Mesh cache", exact: true })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Frame pacing", exact: true })).toHaveCount(0);
  await navigation.getByRole("link", { name: "Textures 04" }).click();
  await expect(page.getByRole("heading", { name: "Texture cache", exact: true })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Mesh cache", exact: true })).toHaveCount(0);
  await expect(page.locator(".lcars")).toHaveAttribute("data-lcars-theme", "voyager");
  await page.goBack();
  await expect(page.getByRole("heading", { name: "World", exact: true })).toBeVisible();
  await page.goBack();
  await page.goBack();
  await expect(page.getByRole("heading", { name: "Overview", exact: true })).toBeVisible();
  await page.goForward();
  await page.goForward();
  await page.goForward();
  await expect(page.getByRole("heading", { name: "Textures", exact: true })).toBeVisible();
  await page.evaluate(() => { window.location.hash = "#objects"; });
  await expect(page.getByRole("heading", { name: "World", exact: true })).toBeVisible();
  await navigation.getByRole("link", { name: "History 06" }).click();
  await expect(page.locator(".lcars-time-trend")).toHaveCount(1);
  await page.getByRole("combobox", { name: "Chart", exact: true }).selectOption("workingSet");
  await expect(page.locator(".lcars-time-trend")).toHaveAttribute("aria-label", "Working set (MiB)");
  await navigation.getByRole("link", { name: "Overview 01" }).click();
  await expect(page.getByRole("heading", { name: "Overview", exact: true })).toBeVisible();
  expect(await page.evaluate(() => window.__dashboardSources.length)).toBe(1);
  await page.screenshot({ path: "test-results/overview-page.png", fullPage: true });
});

test("a tab older than its launcher reloads itself once, then shows a real error rather than looping", async ({ page }) => {
  await page.addInitScript(() => {
    sessionStorage.setItem("u9gl.loads", String(Number(sessionStorage.getItem("u9gl.loads") ?? "0") + 1));
    window.__dashboardSources = [];
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.__dashboardSources.push(this); }
      close() {}
    };
  });
  await page.goto("/");
  await page.waitForFunction(() => window.__dashboardSources.length === 1);
  const emit = payload => page.evaluate(data => window.__dashboardSources[0].dispatchEvent(new MessageEvent("telemetry", { data: JSON.stringify(data) })), payload);
  await emit(replayView());
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "live");
  await Promise.all([page.waitForEvent("load"), emit({ ...replayView(), build: "fedcba9876543210" })]);
  expect(await page.evaluate(() => sessionStorage.getItem("u9gl.loads"))).toBe("2");
  await page.waitForFunction(() => window.__dashboardSources.length === 1);
  await emit({ ...replayView(), version: 99 });
  await expect(page.locator("[data-feed-state]")).toHaveAttribute("data-feed-state", "error");
  expect(await page.evaluate(() => sessionStorage.getItem("u9gl.loads"))).toBe("2");
});

test("the Events page follows the game log with filters and folded details", async ({ page }) => {
  const event = (seq, tag, level, text, detail = null) => ({ seq, receivedUtc: "2026-11-03T20:00:00.0000000Z", runPid: 42,
    source: level === "fault" ? "crash" : "log", tag, level, text, detail });
  const summary = { runPid: 42, runVersion: "0.5.3+test", faults: 1, warnings: 1, lastSeq: 3, logFile: "u9log.txt", logBytes: 100, logGrewAgoMs: 2000, crashReport: null };
  const pages = [
    { events: [event(1, "CFG", "info", "CFG  loaded u9gl.ini"), event(2, "GLPOST", "warning", "GLPOST shader failed")], next: 2, oldest: 1, summary },
    { events: [event(3, "FAULT", "fault", "FAULT: ACCESS_VIOLATION (0xC0000005)", "at 0x005C3FA0 = u9-laa.exe+0x1C3FA0")], next: 3, oldest: 1, summary },
  ];
  const asked = [];
  await page.route("**/api/v1/log**", route => {
    const after = Number(new URL(route.request().url()).searchParams.get("after"));
    asked.push(after);
    const body = after === 0 ? pages[0] : after === 2 ? pages[1] : { events: [], next: after, oldest: 1, summary };
    return route.fulfill({ contentType: "application/json", body: JSON.stringify(body) });
  });
  await page.goto("/#events");
  await expect(page.getByRole("heading", { name: "Events", exact: true })).toBeVisible();
  const list = page.getByRole("list", { name: "Game log events" });
  await expect(list.locator("li")).toHaveCount(3);
  expect(asked.slice(0, 2)).toEqual([0, 2]);
  await expect(page.getByText("1 faults · 1 warnings this run")).toBeVisible();
  await page.getByRole("combobox", { name: "Level" }).selectOption("fault");
  await expect(list.locator("li")).toHaveCount(1);
  await list.locator("summary").click();
  await expect(list.locator("pre")).toHaveText("at 0x005C3FA0 = u9-laa.exe+0x1C3FA0");
  await page.getByRole("combobox", { name: "Level" }).selectOption("all");
  await page.getByRole("searchbox", { name: "Filter" }).fill("shader");
  await expect(list.locator("li")).toHaveCount(1);
  await expect(list.locator(".u9gl-event-text")).toHaveText("shader failed");
  await page.setViewportSize({ width: 320, height: 900 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
});

test("a texture list holding the same texture twice still draws, and a page error stays on its page", async ({ page }) => {
  await page.addInitScript(() => {
    window.__dashboardSources = [];
    window.EventSource = class extends EventTarget {
      constructor() { super(); window.__dashboardSources.push(this); }
      close() {}
    };
  });
  await page.goto("/#textures");
  await page.waitForFunction(() => window.__dashboardSources.length === 1);
  const view = replayView();
  const parts = [{ name: "id", bits: 16 }, { name: "frame", bits: 8 }, { name: "width", bits: 12 }, { name: "height", bits: 12 },
    { name: "age", bits: 12 }, { name: "overridden", bits: 1 }, { name: "keyed", bits: 1 }];
  const word = (id, keyed) => String(BigInt(id) | 64n << 24n | 64n << 36n | 1n << 48n | BigInt(keyed) << 61n);
  view.arrays = { textureList: { name: "textures", countField: "count", capacity: 256, parts } };
  view.current.groups.textureList = { collectedQpc: "2000", elapsedMs: 1000, ageMs: 0, quality: "ready", staleMs: 12000,
    values: { cacheEntries: "300", count: "3" }, array: [word(8040, 0), word(8040, 1), word(8020, 0)] };
  await page.evaluate(v => window.__dashboardSources[0].dispatchEvent(new MessageEvent("telemetry", { data: JSON.stringify(v) })), view);
  await expect(page.getByRole("table", { name: "Textures in use" }).locator("tbody tr")).toHaveCount(3);
  await expect(page.getByRole("navigation", { name: "Dashboard navigation" })).toBeVisible();
});
