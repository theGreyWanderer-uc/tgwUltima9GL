import { test, expect } from "@playwright/test";

test.beforeEach(async ({ page }) => {
  await page.goto("/");
  await page.evaluate(() => document.fonts.ready);
});

async function frameGeometry(page) {
  return page.evaluate(() => {
    const controls = document.querySelector(".lcars-navigation-controls");
    const nav = document.querySelector(".lcars-navigation");
    const brand = document.querySelector(".lcars-sidebar-brand");
    const tail = document.querySelector(".lcars-sidebar-tail");
    const content = document.querySelector(".lcars-shell-content");
    const first = controls.querySelector("a");
    const last = [...controls.querySelectorAll("a")].at(-1);
    const rect = element => { const r = element.getBoundingClientRect(); return { top: r.top, bottom: r.bottom }; };
    return {
      controls: rect(controls), brand: rect(brand), tail: rect(tail), content: rect(content), first: rect(first), last: rect(last),
      before: { height: parseFloat(getComputedStyle(controls, "::before").height), color: getComputedStyle(controls, "::before").backgroundColor },
      after: { height: parseFloat(getComputedStyle(controls, "::after").height), color: getComputedStyle(controls, "::after").backgroundColor },
      gap: parseFloat(getComputedStyle(nav).rowGap), position: getComputedStyle(controls).position,
      rootSize: parseFloat(getComputedStyle(document.documentElement).fontSize),
      railColor: getComputedStyle(tail).backgroundColor,
      activeColor: getComputedStyle(nav.querySelector("[aria-current]")).backgroundColor,
      overflow: document.documentElement.scrollWidth > innerWidth + 1,
    };
  });
}

test("joined frame keeps full-height rail and separated frozen links across themes, densities and text sizes", async ({ page }) => {
  test.setTimeout(90000);
  await page.setViewportSize({ width: 1440, height: 1000 });
  await expect(page.locator("h1")).toHaveCount(1);
  await expect(page.getByRole("main")).toHaveCount(1);
  await expect(page.locator(".lcars-display-frame")).toHaveAttribute("data-sticky-sidebar", "true");
  await page.getByRole("navigation", { name: "Dashboard navigation" }).getByRole("link", { name: "History 06" }).click();
  await page.locator(".lcars-time-trend summary").first().click();
  const themes = await page.getByRole("combobox", { name: "Theme", exact: true }).locator("option").evaluateAll(options => options.map(option => option.value));
  for (const theme of themes) {
    await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption(theme);
    for (const density of ["comfortable", "compact"]) {
      await page.getByRole("combobox", { name: "Density", exact: true }).selectOption(density);
      for (const size of [16, 32]) {
        await page.evaluate(px => { document.documentElement.style.fontSize = px + "px"; window.scrollTo(0, 0); }, size);
        const before = await frameGeometry(page);
        expect(before.first.top - before.brand.bottom).toBeCloseTo(before.gap, 0);
        expect(before.before.height).toBeCloseTo(before.gap, 0);
        expect(before.after.height).toBeCloseTo(before.gap, 0);
        expect(before.activeColor).toBe(before.railColor);
        await page.evaluate(() => window.scrollTo(0, 700));
        const after = await frameGeometry(page);
        expect(after.position).toBe("sticky");
        expect(after.controls.top).toBeCloseTo(size, 0);
        expect(after.before.height).toBeCloseTo(before.gap, 0);
        expect(after.after.height).toBeCloseTo(before.gap, 0);
        expect(after.before.color).toBe(after.after.color);
        expect(after.activeColor).toBe(after.railColor);
        expect(after.tail.bottom).toBeCloseTo(after.content.bottom, 0);
        expect(after.overflow).toBe(false);
      }
    }
  }
  await page.evaluate(() => { document.documentElement.style.fontSize = "16px"; window.scrollTo(0, 0); });
  await page.getByRole("checkbox", { name: "Freeze navigation" }).uncheck();
  await page.evaluate(() => window.scrollTo(0, 700));
  const unfrozen = await frameGeometry(page);
  expect(unfrozen.position).toBe("static");
  expect(unfrozen.tail.bottom).toBeCloseTo(unfrozen.content.bottom, 0);
});

test("responsive frame and native controls fit 320, 768 and 1440px at normal and doubled text", async ({ page }) => {
  test.setTimeout(60000);
  for (const theme of ["tng", "cardassian"]) {
    await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption(theme);
    for (const density of ["comfortable", "compact"]) {
      await page.getByRole("combobox", { name: "Density", exact: true }).selectOption(density);
      for (const width of [320, 768, 1440]) {
        await page.setViewportSize({ width, height: 1000 });
        for (const size of [16, 32]) {
          await page.evaluate(px => { document.documentElement.style.fontSize = px + "px"; window.scrollTo(0, 0); }, size);
          const geometry = await frameGeometry(page);
          expect(geometry.overflow, `${theme} ${density} ${width}px ${size}px text`).toBe(false);
          expect(geometry.position).toBe(width <= 700 ? "static" : "sticky");
          await expect(page.getByRole("heading", { name: "U9GL diagnostics", level: 1 })).toBeVisible();
          await expect(page.getByRole("combobox", { name: "Theme", exact: true })).toBeVisible();
          if (width === 320) {
            expect(await page.locator(".lcars-sidebar-brand").isVisible()).toBe(false);
            expect(await page.locator(".lcars-display-code").evaluate(element => element.getBoundingClientRect().top >= document.querySelector("h1").getBoundingClientRect().bottom)).toBe(true);
          }
        }
      }
    }
  }
  await page.screenshot({ path: "test-results/joined-frame-wide-200.png", fullPage: true });
  await page.setViewportSize({ width: 320, height: 1000 });
  await page.screenshot({ path: "test-results/joined-frame-narrow-200.png", fullPage: true });
});

test("short-screen keyboard navigation reaches every frozen link with visible focus", async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 240 });
  await page.evaluate(() => { document.documentElement.style.fontSize = "32px"; });
  const links = page.getByRole("navigation", { name: "Dashboard navigation" }).getByRole("link");
  const count = await links.count();
  await links.first().focus();
  for (let i = 0; i < count; i++) {
    await expect(links.nth(i)).toBeFocused();
    const box = await links.nth(i).boundingBox();
    expect(box.y).toBeGreaterThanOrEqual(0);
    expect(box.y + box.height).toBeLessThanOrEqual(241);
    expect(await links.nth(i).evaluate(element => getComputedStyle(element).outlineStyle)).not.toBe("none");
    if (i < count - 1) await page.keyboard.press("Tab");
  }
  expect(await page.locator(".lcars-navigation-items").evaluate(element => element.scrollTop)).toBeGreaterThan(0);
  await page.keyboard.press("Shift+Tab");
  await expect(links.nth(count - 2)).toBeFocused();
  await page.keyboard.press("Tab");
  await page.keyboard.press("Enter");
  await expect(page).toHaveURL(/#metrics$/);
  await expect(links.nth(count - 1)).toHaveAttribute("aria-current", "location");
  await page.screenshot({ path: "test-results/joined-frame-short-keyboard.png" });
});
