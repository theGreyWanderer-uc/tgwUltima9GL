import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const out = process.argv[2];
if (!out) { console.error("usage: node export-fixtures.mjs <output-dir>"); process.exit(2); }
const suite = JSON.parse(readFileSync(fileURLToPath(new URL("samples.json", import.meta.url)), "utf8"));
for (const sample of suite.cases.filter(c => c.status === "Accepted")) {
  writeFileSync(join(out, `${sample.name}.bin`), Buffer.from(sample.hex, "hex"));
  const lines = Object.entries(sample.fields).map(([key, value]) => `${key} ${value}`);
  lines.push(`session_id ${sample.sessionId}`, `producer_version ${sample.producerVersion}`, `skipped ${sample.skipped}`);
  writeFileSync(join(out, `${sample.name}.expected`), lines.join("\n") + "\n");
  console.log(sample.name);
}
