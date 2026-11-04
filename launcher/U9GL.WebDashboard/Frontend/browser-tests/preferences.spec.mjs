import { test, expect } from "@playwright/test";

const key = "u9gl.dashboard.appearance.v1";
async function expectAppearance(page, theme, density, frozen) {
  await expect(page.getByRole("combobox", { name: "Theme", exact: true })).toHaveValue(theme);
  await expect(page.getByRole("combobox", { name: "Density", exact: true })).toHaveValue(density);
  await expect(page.getByRole("checkbox", { name: "Freeze navigation" })).toBeChecked({ checked: frozen });
  await expect(page.locator(".lcars")).toHaveAttribute("data-lcars-theme", theme);
  await expect(page.locator(".lcars")).toHaveAttribute("data-lcars-density", density);
}

test("last appearance survives refresh and a new browser page", async ({ page, context, baseURL }) => {
  await page.goto("/");
  await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption("voyager");
  await page.getByRole("combobox", { name: "Density", exact: true }).selectOption("compact");
  await page.getByRole("checkbox", { name: "Freeze navigation" }).uncheck();
  await page.reload();
  await expectAppearance(page, "voyager", "compact", false);
  const reopened = await context.newPage();
  await reopened.goto(baseURL);
  await expectAppearance(reopened, "voyager", "compact", false);
  await reopened.close();
  await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption("cardassian");
  await page.getByRole("combobox", { name: "Density", exact: true }).selectOption("comfortable");
  await page.getByRole("checkbox", { name: "Freeze navigation" }).check();
  await page.reload();
  await expectAppearance(page, "cardassian", "comfortable", true);
});

test("malformed, outdated or invalid saved preferences fall back to supported values", async ({ page }) => {
  await page.goto("/");
  for (const [saved, theme, density, frozen] of [
    ["{", "tng", "comfortable", true],
    [JSON.stringify({ version: 99, theme: "voyager", density: "compact", freezeNavigation: false }), "tng", "comfortable", true],
    [JSON.stringify({ version: 1, theme: "unknown", density: "small", freezeNavigation: "false" }), "tng", "comfortable", true],
    [JSON.stringify({ version: 1, theme: "voyager", density: "invalid", freezeNavigation: false }), "voyager", "comfortable", false],
  ]) {
    await page.evaluate(({ key, saved }) => localStorage.setItem(key, saved), { key, saved });
    await page.reload();
    await expectAppearance(page, theme, density, frozen);
  }
});

test("blocked browser storage does not break the dashboard controls", async ({ page }) => {
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.addInitScript(() => {
    for (const method of ["getItem", "setItem"]) Object.defineProperty(Storage.prototype, method, {
      value() { throw new DOMException("Storage is blocked", "SecurityError"); },
    });
  });
  await page.goto("/");
  await expectAppearance(page, "tng", "comfortable", true);
  await page.getByRole("combobox", { name: "Theme", exact: true }).selectOption("voyager");
  await page.getByRole("combobox", { name: "Density", exact: true }).selectOption("compact");
  await page.getByRole("checkbox", { name: "Freeze navigation" }).uncheck();
  await expectAppearance(page, "voyager", "compact", false);
  expect(errors).toEqual([]);
});
