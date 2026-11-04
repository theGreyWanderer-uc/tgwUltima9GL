import { defineConfig } from "@playwright/test";

const browser = process.env.U9GL_TEST_BROWSER || "chromium";

export default defineConfig({
  testDir: "./browser-tests",
  workers: 1,
  use: {
    baseURL: process.env.U9GL_DASHBOARD_URL || "http://127.0.0.1:10099",
    browserName: browser,
    ...(browser === "chromium" ? { channel: process.env.U9GL_BROWSER_CHANNEL || "msedge" } : {}),
    viewport: { width: 1440, height: 1000 },
  },
});
