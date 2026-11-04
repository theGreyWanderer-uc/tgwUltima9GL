import { defineConfig } from "vite";

export default defineConfig({
  plugins: [{
    name: "u9gl-client-component-directive",
    enforce: "pre",
    transform(code, id) {
      if (id.replaceAll("\\", "/").endsWith("/components/ui/lcars/lcars-data-table.tsx"))
        return { code: code.replace(/^"use client";/, "             "), map: null };
    },
  }],
  base: "/",
  build: {
    outDir: "dist",
    modulePreload: { polyfill: false },
  },
  server: { host: "127.0.0.1", proxy: { "/api": "http://127.0.0.1:9999" } },
});
