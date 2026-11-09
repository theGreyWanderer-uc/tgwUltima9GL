import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";

const HEADER = 160, ENTRY = 16, MAPPING = 65536;
const headerLayout = [
  ["magic", 0, 4], ["major", 4, 2], ["minor", 6, 2], ["header_bytes", 8, 4], ["snapshot_bytes", 12, 4],
  ["sequence", 16, 4], ["producer_pid", 20, 4], ["process_created_filetime", 24, 8],
  ["qpc_frequency", 48, 8], ["session_start_qpc", 56, 8], ["sample_qpc", 64, 8],
  ["sample_utc_filetime", 72, 8], ["sample_index", 80, 8], ["producer_kind", 88, 4],
  ["group_count", 92, 2], ["entry_bytes", 94, 2], ["mapping_bytes", 128, 4], ["requested_groups", 132, 4],
];
const schema = [
  { id: 1, name: "frames", version: 1, fields: ["frame_no", "presents", "present_failed", "draw_calls_last", "vertices_last"] },
  { id: 2, name: "resources", version: 1, fields: ["textures_alive", "texture_bytes", "textures_created", "textures_destroyed"] },
  { id: 3, name: "memory", version: 1, fields: ["private_bytes", "working_set", "peak_working_set", "pagefile"] },
  { id: 4, name: "pacing", version: 1, fields: ["fps_milli"] },
  { id: 5, name: "renderer", version: 1, fields: ["mip_mode", "anisotropy_milli", "depth_mode", "buffers_alive", "buffer_bytes", "fbos_alive"] },
  { id: 6, name: "world", version: 1, fields: ["map_valid", "map", "avatar_valid", "avatar_x", "avatar_y", "avatar_z"] },
  { id: 7, name: "textureCache", version: 1, fields: ["cached", "cache_bytes", "budget_bytes", "pending", "uploads", "duplicates",
    "evictions", "uploads_per_sec", "evictions_per_sec", "upload_max_us"] },
  { id: 8, name: "overrides", version: 1, fields: ["enabled", "files", "used", "rejected", "load_max_us"] },
  { id: 9, name: "memoryTrend", version: 1, fields: ["floor_slope_milli", "raw_slope_milli", "span_frames", "window_samples",
    "window_capacity", "floor_rising"] },
  { id: 10, name: "area", version: 1, fields: ["grid_w", "grid_h", "cells_drawn", "cells_active", "cells_collide", "cells_queued", "cells_with_slots", "slot_bytes", "lru_cells", "ring_near", "ring_mid", "ring_far", "terrain_meshes", "terrain_mesh_max", "terrain_a", "terrain_b", "queue_sorting", "queue_waiting", "queue_ready", "pending_meshes", "pending_instances", "objects", "worst_cell", "worst_cell_capacity", "drawn", "moving", "fixed", "nonfixed", "unresolved", "unique_types", "top_type1", "top_count1", "top_type2", "top_count2", "top_type3", "top_count3", "in_camera", "mesh_types", "mesh_in_use", "mesh_idle", "mesh_users", "mesh_max_users", "mesh_max_appearance", "mesh_bytes_pool", "mesh_bytes_outside", "pool_size", "pool_largest_free", "engine_mesh_types", "engine_mesh_bytes", "idle_keep", "fixed_used", "fixed_max", "nonfixed_used", "nonfixed_max", "sample_us"] },
  { id: 11, name: "addressSpace", version: 1, fields: ["free_bytes", "largest_free_bytes", "walk_us"] },
  { id: 12, name: "vram", version: 2, fields: ["source", "dedicated_bytes", "local_usage_bytes", "local_budget_bytes", "shared_usage_bytes", "shared_budget_bytes", "query_us"] },
  { id: 13, name: "overrideResident", version: 1, fields: ["count", "bytes"] },
  { id: 16, name: "textureList", version: 1, fields: ["cache_entries", "count"] },
  { id: 17, name: "meshList", version: 1, fields: ["mesh_types", "count"] },
  { id: 18, name: "player", version: 3, fields: ["record_valid", "strength", "dexterity", "intelligence", "health", "health_max", "health_base", "mana", "mana_max", "mana_base", "breath", "breath_max", "breath_base", "armour", "armour_bonus", "magic_resist_bonus", "state_flags", "weapon", "skill_fists", "skill_one_handed", "skill_two_handed", "skill_staff", "skill_ranged", "datums_valid", "class_chosen", "class_id", "shrines", "spells_lo", "spells_hi", "equip_left_hand", "equip_right_hand", "equip_scabbard", "equip_left_shoulder", "equip_right_shoulder", "equip_quiver", "equip_left_forearm", "equip_hands_class", "gold", "arrows", "active_spells", "spell_discount", "combat_states", "stun_ms"] },
  { id: 15, name: "areaCells", version: 1, fields: ["grid_w", "grid_h", "avatar_valid", "avatar_x", "avatar_y", "slot_capacity", "cells_outside", "count"] },
  { id: 14, name: "gpu", version: 1, fields: ["frame_no", "samples", "skipped", "support", "frame_us", "region_mask", "scene_us", "beam_us", "rays_us", "bloom_us", "ao_us", "pool_shadows_us", "sun_shadows_us", "water_us", "composite_us"] },
];
const i64 = value => String(BigInt.asUintN(64, BigInt(value)));
const known = (id, version) => schema.find(g => g.id === id && g.version === version);
const baseHeader = {
  magic: "1297365333", major: "2", minor: "0", header_bytes: "160", sequence: "2",
  producer_pid: "4242", process_created_filetime: "134061984000000000",
  qpc_frequency: "10000000", session_start_qpc: "100000000", sample_qpc: "110000000",
  sample_utc_filetime: "134061984010000000", sample_index: "1", producer_kind: "2",
  entry_bytes: "16", mapping_bytes: "65536", requested_groups: "0",
};
const baseGroups = () => [
  { id: 1, version: 1, flags: 1, collected: "110000000", values: ["600", "599", "1", "1200", "3600"] },
  { id: 2, version: 1, flags: 1, collected: "110000000", values: ["128", "67108864", "140", "12"] },
  { id: 3, version: 1, flags: 1, collected: "105000000", values: ["536870912", "402653184", "603979776", "536870912"] },
];
const version = "0.6.0+fixture";
const cases = [];

function addSample(name, status = "Accepted", { header = {}, groups = baseGroups(), before = 2, after = 2,
  session = "00112233445566778899aabbccddeeff", mutate = () => {}, length } = {}) {
  let at = HEADER + groups.length * ENTRY;
  const placed = groups.map(g => {
    const words = [g.collected, ...g.values, ...(g.extra ?? [])];
    const bytes = g.bytes ?? words.length * 8;
    const offset = g.offset ?? at;
    at = Math.max(at, offset + bytes);
    return { ...g, words, bytes, offset };
  });
  const fields = { ...baseHeader, group_count: String(groups.length), snapshot_bytes: String(at), ...header };
  const size = length ?? Number(fields.snapshot_bytes);
  const buffer = Buffer.alloc(Math.max(size, at, HEADER));
  for (const [field, offset, width] of headerLayout) {
    const value = BigInt(fields[field]);
    if (width === 2) buffer.writeUInt16LE(Number(value), offset);
    else if (width === 4) buffer.writeUInt32LE(Number(value), offset);
    else buffer.writeBigUInt64LE(value, offset);
  }
  Buffer.from(session, "hex").copy(buffer, 32);
  Buffer.from(version, "ascii").copy(buffer, 96);
  placed.forEach((g, i) => {
    const entry = HEADER + i * ENTRY;
    buffer.writeUInt16LE(g.id, entry); buffer.writeUInt16LE(g.version, entry + 2);
    buffer.writeUInt32LE(g.offset, entry + 4); buffer.writeUInt32LE(g.bytes, entry + 8);
    buffer.writeUInt32LE(g.flags, entry + 12);
    if (g.flags & 1) g.words.forEach((w, j) => { if (j * 8 < g.bytes) buffer.writeBigUInt64LE(BigInt(w), g.offset + j * 8); });
  });
  mutate(buffer);
  const bytes = buffer.subarray(0, size);
  let expected = null, skipped = 0;
  if (status === "Accepted") {
    expected = Object.fromEntries(headerLayout.map(([field]) => ["header." + field, fields[field]]));
    for (const g of placed) {
      const s = known(g.id, g.version);
      if (!s) { skipped++; continue; }
      if (!(g.flags & 1)) continue;
      expected[s.name + ".collected_qpc"] = g.collected;
      s.fields.forEach((f, j) => { expected[s.name + "." + f] = g.values[j]; });
    }
  }
  cases.push({
    name, status, sequenceBefore: String(before), sequenceAfter: String(after), hex: bytes.toString("hex"),
    fields: expected, skipped, sessionId: session, producerVersion: version,
  });
}

const groups = edit => { const g = baseGroups(); edit(g); return g; };
const zeroed = g => ({ ...g, values: g.values.map(() => "0") });
const unavailable = g => ({ ...g, flags: 0 });

addSample("normal");
addSample("next", "Accepted", { before: 4, after: 4, header: { sequence: "4", sample_index: "2", sample_qpc: "111000000" },
  groups: groups(g => { g[0].collected = "111000000"; g[0].values[0] = "606"; g[0].values[1] = "605"; }) });
addSample("gap", "Accepted", { before: 8, after: 8, header: { sequence: "8", sample_index: "4", sample_qpc: "113000000" },
  groups: groups(g => { g[0].collected = "113000000"; g[0].values[0] = "618"; g[0].values[1] = "617"; }) });
addSample("zero-counters", "Accepted", { groups: baseGroups().map(zeroed) });
addSample("memory-unavailable", "Accepted", { groups: groups(g => { g[2] = unavailable(g[2]); }) });
addSample("all-groups-unavailable", "Accepted", { groups: baseGroups().map(unavailable) });
addSample("no-groups", "Accepted", { groups: [] });
addSample("utc-unavailable", "Accepted", { header: { sample_utc_filetime: "0" } });
addSample("large-counters", "Accepted", { groups: groups(g => {
  g[0].values[0] = "18446744073709551615"; g[0].values[1] = "9007199254740993"; g[0].values[4] = "4294967296";
  g[1].values[1] = "18446744073709551615"; g[2].values[0] = "4294967296"; }) });
addSample("sequence-wrapped", "Accepted", { before: 0, after: 0, header: { sequence: "0", sample_index: "2147483648" } });
addSample("restart", "Accepted", { session: "ffeeddccbbaa99887766554433221100", header: {
  process_created_filetime: "134061984100000000", sample_utc_filetime: "134061984110000000",
  session_start_qpc: "200000000", sample_qpc: "210000000" },
  groups: groups(g => { g[0].collected = g[1].collected = "210000000"; g[2].collected = "205000000"; }) });
addSample("requested-groups", "Accepted", { header: { requested_groups: String(0b1110 | 2 ** 30) } });
addSample("future-minor", "Accepted", { header: { minor: "7" } });
addSample("unknown-group-skipped", "Accepted", { groups: groups(g => {
  g.push({ id: 30, version: 1, flags: 1, collected: "110000000", values: ["1", "2", "3"] }); }) });
addSample("unknown-group-version", "Accepted", { groups: groups(g => { g[0].version = 2; }) });
addSample("all-game-groups", "Accepted", { groups: groups(g => g.push(
  { id: 4, version: 1, flags: 1, collected: "109000000", values: ["59987"] },
  { id: 5, version: 1, flags: 1, collected: "110000000", values: ["2", "8000", "1", "0", "0", "0"] },
  { id: 6, version: 1, flags: 1, collected: "110000000", values: ["1", "9", "1", i64(-1234567), "2345678", i64(-5)] },
  { id: 7, version: 1, flags: 1, collected: "109000000", values: ["812", "176160768", "201326592", "3", "40210", "77", "1503", "12", "4", "16250"] },
  { id: 8, version: 1, flags: 1, collected: "109000000", values: ["1", "311", "290", "2", "8700"] },
  { id: 9, version: 1, flags: 1, collected: "105000000", values: [i64(-1536250), "2000", "480", "16", "16", "0"] },
)) });
addSample("sampled-groups", "Accepted", { groups: groups(g => g.push(
  { id: 10, version: 1, flags: 1, collected: "109000000", values: ["10001", "10002", "10003", "10004", "10005", "10006", "10007", "10008", "10009", "10010", "10011", "10012", "10013", "10014", "10015", "10016", "10017", "10018", "10019", "10020", "10021", "10022", "10023", "10024", "10025", "10026", "10027", "10028", "10029", "10030", "10031", "10032", "10033", "10034", "10035", "10036", "10037", "10038", "10039", "10040", "10041", "10042", "10043", "10044", "10045", "10046", "10047", "10048", "10049", "10050", "10051", "10052", "10053", "10054", "10055"] },
  { id: 11, version: 1, flags: 1, collected: "109000000", values: ["11001", "11002", "11003"] },
  { id: 12, version: 2, flags: 1, collected: "109000000", values: ["1", "8589934592", "312000000", "7000000000", "20000000", "16000000000", "42"] },
  { id: 13, version: 1, flags: 1, collected: "109000000", values: ["13001", "13002"] }
)) });
addSample("vram-version-1-skipped", "Accepted", { groups: groups(g => g.push(
  { id: 12, version: 1, flags: 1, collected: "109000000", values: ["1", "8589934592", "4294967296", "0", "0"] })) });
addSample("gpu-sample", "Accepted", { groups: groups(g => g.push(
  { id: 14, version: 1, flags: 1, collected: "108000000", values: ["590", "12", "1", "1", "9875", "507", "1000", "1111", "1222", "1333", "1444", "1555", "1666", "1777", "1888"] })) });
addSample("area-cells", "Accepted", { groups: groups(g => g.push(
  { id: 15, version: 1, flags: 1, collected: "108000000", values: ["128", "128", "1", "62", "42", "2045", "0", "2"],
    extra: [String(63n | 41n << 8n | 212n << 16n | 180n << 32n | 3n << 44n | 7n << 56n), String(127n | 2045n << 16n)] })) });
addSample("lists", "Accepted", { groups: groups(g => g.push(
  { id: 16, version: 1, flags: 1, collected: "108000000", values: ["812", "2"],
    extra: [String(5448n | 3n << 16n | 256n << 24n | 128n << 36n | 5n << 48n | 1n << 61n), String(8047n | 4095n << 24n | 64n << 36n | 1n << 60n)] },
  { id: 17, version: 1, flags: 1, collected: "108000000", values: ["900", "1"],
    extra: [String(4711n | 120n << 16n | 65536n << 32n | 1n << 60n)] })) });
addSample("player", "Accepted", { groups: groups(g => g.push(
  { id: 18, version: 3, flags: 1, collected: "108000000", values: ["1", "1", i64(-1), "3", "85", "120", "110", "40", "60", "50", "7", "30", "30",
    "12", "3", "5", "2147483650", i64(-1), "3", "1", "0", "2", "1", "1", "1", "5", "129", "9223372036854775809", "8192",
    "5689", "1298", "0", "0", "0", "5651", "4882", "4", "58", "11", "2048", "0", "288", "1500"] })) });
addSample("area-cells-count-past-payload", "Invalid", { groups: groups(g => g.push(
  { id: 15, version: 1, flags: 1, collected: "108000000", values: ["128", "128", "0", "0", "0", "2045", "0", "3"], extra: ["1"] })) });
addSample("world-before-load", "Accepted", { groups: groups(g => g.push(
  { id: 6, version: 1, flags: 1, collected: "110000000", values: ["0", "0", "0", "0", "0", "0"] })) });
addSample("group-extended", "Accepted", { groups: groups(g => { g[2].extra = ["777", "888"]; }) });

addSample("uninitialized", "Uninitialized", { before: 0, after: 0, mutate: b => b.fill(0) });
addSample("busy", "Busy", { before: 3, after: 3, header: { sequence: "3" } });
addSample("changed", "Changed", { before: 2, after: 4 });
addSample("copied-sequence-mismatch", "Changed", { header: { sequence: "4" } });
addSample("bad-magic", "Incompatible", { header: { magic: "1" } });
addSample("old-major", "Incompatible", { header: { major: "1" } });
addSample("future-major", "Incompatible", { header: { major: "3" } });
addSample("wrong-header-size", "Incompatible", { header: { header_bytes: "128" } });
addSample("wrong-entry-size", "Incompatible", { header: { entry_bytes: "24" } });
addSample("wrong-mapping-size", "Incompatible", { header: { mapping_bytes: "4096" } });
addSample("truncated", "Incompatible", { length: 159 });
addSample("size-mismatch", "Incompatible", { length: 344 });
addSample("zero-pid", "Invalid", { header: { producer_pid: "0" } });
addSample("zero-session", "Invalid", { session: "0".repeat(32) });
addSample("zero-frequency", "Invalid", { header: { qpc_frequency: "0" } });
addSample("zero-sample-index", "Invalid", { header: { sample_index: "0" } });
addSample("clock-before-session", "Invalid", { header: { sample_qpc: "99999999" } });
addSample("collection-in-future", "Invalid", { groups: groups(g => { g[2].collected = "110000001"; }) });
addSample("collection-before-session", "Invalid", { groups: groups(g => { g[0].collected = "99999999"; }) });
addSample("unknown-producer", "Invalid", { header: { producer_kind: "99" } });
addSample("non-ascii-version", "Invalid", { mutate: b => { b[96] = 255; } });
addSample("unterminated-version", "Invalid", { mutate: b => b.fill(120, 96, 128) });
addSample("version-padding", "Invalid", { mutate: b => { b[127] = 1; } });
addSample("reserved-nonzero", "Invalid", { mutate: b => { b[159] = 1; } });
addSample("too-many-groups", "Invalid", { header: { group_count: "33" }, length: 336 });
addSample("directory-past-end", "Invalid", { header: { group_count: "20" } });
addSample("unavailable-nonzero", "Invalid", { groups: groups(g => { g[2] = unavailable(g[2]); }),
  mutate: b => { b.writeBigUInt64LE(1n, 296 + 8); } });
addSample("unknown-flags", "Invalid", { groups: groups(g => { g[1].flags = 3; }) });
addSample("group-id-zero", "Invalid", { groups: groups(g => { g[0].id = 0; }) });
addSample("group-ids-not-ascending", "Invalid", { groups: groups(g => { [g[0], g[1]] = [g[1], g[0]]; }) });
addSample("group-misaligned", "Invalid", { groups: groups(g => { g[2].offset = 300; }) });
addSample("groups-overlap", "Invalid", { groups: groups(g => { g[1].offset = 240; }) });
addSample("group-inside-directory", "Invalid", { groups: groups(g => { g[0].offset = 200; }) });
addSample("group-past-end", "Invalid", { header: { snapshot_bytes: "328" } });
addSample("group-shorter-than-version", "Invalid", { groups: groups(g => { g[2].bytes = 32; }) });
addSample("group-size-not-words", "Invalid", { groups: groups(g => { g[2].bytes = 44; }) });

const result = {
  format: "u9gl-telemetry-fixtures-v2", headerBytes: HEADER, entryBytes: ENTRY, mappingBytes: MAPPING,
  headerLayout: headerLayout.map(([name, offset, bytes]) => ({ name, offset, bytes })),
  groups: schema,
  cases,
  replays: [
    { name: "gap-and-restart", samples: ["normal", "next", "gap", "restart"],
      expected: "One missed sample between next and gap; restart has a new process/session identity despite reusing PID 4242." },
    { name: "stale-live-process", samples: ["normal"], nowQpc: "140000001", processAlive: true,
      expected: "Stale, not a crash; retain the last good sample. Memory is older than the frame group." },
    { name: "publisher-exited", samples: ["normal", "busy"], nowQpc: "140000001", processAlive: false,
      expected: "Process exited; retain normal. Do not wait on the odd sequence or infer a crash without other evidence." },
  ],
};
writeFileSync(fileURLToPath(new URL("samples.json", import.meta.url)), JSON.stringify(result, null, 2) + "\n");
console.log(`Wrote ${cases.length} synthetic telemetry samples.`);
