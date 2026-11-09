#include <string.h>
#include "metrics_snapshot.h"

static void entry(U9TelemetryGameRecord *r, unsigned slot, unsigned id, unsigned version,
                  const void *payload, size_t bytes)
{
    r->directory[slot].id = (uint16_t)id;
    r->directory[slot].version = (uint16_t)version;
    r->directory[slot].offset = (uint32_t)((const unsigned char *)payload - (const unsigned char *)r);
    r->directory[slot].bytes = (uint32_t)bytes;
}

void u9telemetry_game_record_init(U9TelemetryGameRecord *r)
{
    memset(r, 0, sizeof *r);
    r->header.snapshot_bytes = sizeof *r;
    r->header.group_count = U9TM_GAME_GROUPS;
    entry(r, 0, U9TM_GROUP_FRAMES, 1, &r->frames, sizeof r->frames);
    entry(r, 1, U9TM_GROUP_RESOURCES, 1, &r->resources, sizeof r->resources);
    entry(r, 2, U9TM_GROUP_MEMORY, 1, &r->memory, sizeof r->memory);
    entry(r, 3, U9TM_GROUP_PACING, 1, &r->pacing, sizeof r->pacing);
    entry(r, 4, U9TM_GROUP_RENDERER, 1, &r->renderer, sizeof r->renderer);
    entry(r, 5, U9TM_GROUP_WORLD, 1, &r->world, sizeof r->world);
    entry(r, 6, U9TM_GROUP_TEXTURE_CACHE, 1, &r->texture_cache, sizeof r->texture_cache);
    entry(r, 7, U9TM_GROUP_OVERRIDES, 1, &r->overrides, sizeof r->overrides);
    entry(r, 8, U9TM_GROUP_MEMORY_TREND, 1, &r->memory_trend, sizeof r->memory_trend);
    entry(r, 9, U9TM_GROUP_AREA, 1, &r->area, sizeof r->area);
    entry(r, 10, U9TM_GROUP_ADDRESS_SPACE, 1, &r->address_space, sizeof r->address_space);
    entry(r, 11, U9TM_GROUP_VRAM, 2, &r->vram, sizeof r->vram);
    entry(r, 12, U9TM_GROUP_OVERRIDE_RESIDENT, 1, &r->override_resident, sizeof r->override_resident);
    entry(r, 13, U9TM_GROUP_GPU, 1, &r->gpu, sizeof r->gpu);
    entry(r, 14, U9TM_GROUP_AREA_CELLS, 1, &r->area_cells, sizeof r->area_cells);
    entry(r, 15, U9TM_GROUP_TEXTURE_LIST, 1, &r->texture_list, sizeof r->texture_list);
    entry(r, 16, U9TM_GROUP_MESH_LIST, 1, &r->mesh_list, sizeof r->mesh_list);
    entry(r, 17, U9TM_GROUP_PLAYER, 3, &r->player, sizeof r->player);
}

static uint64_t scaled(double value, double scale)
{
    return value > 0.0 ? (uint64_t)(value * scale + 0.5) : 0;
}

static int64_t scaled_signed(double value, double scale)
{
    return (int64_t)(value * scale + (value < 0.0 ? -0.5 : 0.5));
}

static void copy_heavy(const U9TelemetryInputs *in, U9TelemetryGameRecord *out)
{
    const U9MetricsHeavy *f = in->heavy;
    const U9ObjStats *a = in->area;
    if (!f) return;
    if (f->area_qpc && a && a->valid) {
        out->directory[9].flags = U9TM_GROUP_AVAILABLE;
        out->area.collected_qpc = f->area_qpc;
        {
            unsigned i, n = a->meshListCount < U9TM_LIST_ITEMS ? a->meshListCount : U9TM_LIST_ITEMS;
            out->directory[16].flags = U9TM_GROUP_AVAILABLE;
            out->mesh_list.collected_qpc = f->area_qpc;
            out->mesh_list.mesh_types = a->meshTypes;
            out->mesh_list.count = n;
            for (i = 0; i < n; i++) {
                const U9ObjMesh *m = &a->meshList[i];
                out->mesh_list.item[i] = (uint64_t)(m->appearance & 0xFFFFu) |
                    (uint64_t)(m->users > 0xFFFFu ? 0xFFFFu : m->users) << 16 |
                    (uint64_t)(m->size > 0xFFFFFFFu ? 0xFFFFFFFu : m->size) << 32 | (uint64_t)(m->inPool ? 1u : 0u) << 60;
            }
        }
        {
            unsigned i, n = a->cellCount < U9TM_AREA_CELLS ? a->cellCount : U9TM_AREA_CELLS;
            out->directory[14].flags = U9TM_GROUP_AVAILABLE;
            out->area_cells.collected_qpc = f->area_qpc;
            out->area_cells.grid_w = a->gridW;
            out->area_cells.grid_h = a->gridH;
            if (in->avatar_valid && in->avatar_x >= 0 && in->avatar_y >= 0) {
                out->area_cells.avatar_valid = 1;
                out->area_cells.avatar_x = (uint64_t)in->avatar_x / 1024u;
                out->area_cells.avatar_y = (uint64_t)in->avatar_y / 1024u;
            }
            out->area_cells.slot_capacity = in->cell_slots_usable;
            out->area_cells.cells_outside = a->cellsOutside;
            out->area_cells.count = n;
            for (i = 0; i < n; i++) {
                const U9ObjCell *c = &a->cell[i];
                out->area_cells.cell[i] = (uint64_t)(c->x & 0xFFu) | (uint64_t)(c->y & 0xFFu) << 8 |
                    (uint64_t)c->objects << 16 | (uint64_t)(c->drawn & 0xFFFu) << 32 |
                    (uint64_t)(c->moving & 0xFFFu) << 44 | (uint64_t)(c->flags & 0xFFu) << 56;
            }
        }
        out->area.grid_w = (uint64_t)(a->gridW);
        out->area.grid_h = (uint64_t)(a->gridH);
        out->area.cells_drawn = (uint64_t)(a->cellsDrawn);
        out->area.cells_active = (uint64_t)(a->cellsActive);
        out->area.cells_collide = (uint64_t)(a->cellsCollide);
        out->area.cells_queued = (uint64_t)(a->cellsQueued);
        out->area.cells_with_slots = (uint64_t)(a->cellsWithSlots);
        out->area.slot_bytes = (uint64_t)(a->slotBytes);
        out->area.lru_cells = (uint64_t)(a->lruCells);
        out->area.ring_near = (uint64_t)(a->ring[0]);
        out->area.ring_mid = (uint64_t)(a->ring[1]);
        out->area.ring_far = (uint64_t)(a->ring[2]);
        out->area.terrain_meshes = (uint64_t)(a->terrainMeshes);
        out->area.terrain_mesh_max = (uint64_t)(a->terrainMeshMax);
        out->area.terrain_a = (uint64_t)(a->terrainA);
        out->area.terrain_b = (uint64_t)(a->terrainB);
        out->area.queue_sorting = (uint64_t)(a->queue[0]);
        out->area.queue_waiting = (uint64_t)(a->queue[1]);
        out->area.queue_ready = (uint64_t)(a->queue[2]);
        out->area.pending_meshes = (uint64_t)(a->pendingMeshes);
        out->area.pending_instances = (uint64_t)(a->pendingInstances);
        out->area.objects = (uint64_t)(a->objects);
        out->area.worst_cell = (uint64_t)(a->worstCell);
        out->area.worst_cell_capacity = (uint64_t)(in->cell_slots_usable);
        out->area.drawn = (uint64_t)(a->drawn);
        out->area.moving = (uint64_t)(a->moving);
        out->area.fixed = (uint64_t)(a->fixed);
        out->area.nonfixed = (uint64_t)(a->nonfixed);
        out->area.unresolved = (uint64_t)(a->unresolved);
        out->area.unique_types = (uint64_t)(a->uniqueTypes);
        out->area.top_type1 = (uint64_t)(a->topType[0]);
        out->area.top_count1 = (uint64_t)(a->topCount[0]);
        out->area.top_type2 = (uint64_t)(a->topType[1]);
        out->area.top_count2 = (uint64_t)(a->topCount[1]);
        out->area.top_type3 = (uint64_t)(a->topType[2]);
        out->area.top_count3 = (uint64_t)(a->topCount[2]);
        out->area.in_camera = (uint64_t)(a->inCamera);
        out->area.mesh_types = (uint64_t)(a->meshTypes);
        out->area.mesh_in_use = (uint64_t)(a->meshInUse);
        out->area.mesh_idle = (uint64_t)(a->meshIdle);
        out->area.mesh_users = (uint64_t)(a->meshUsers);
        out->area.mesh_max_users = (uint64_t)(a->meshMaxUsers);
        out->area.mesh_max_appearance = (uint64_t)(a->meshMaxAppearance);
        out->area.mesh_bytes_pool = (uint64_t)(a->meshBytesPool);
        out->area.mesh_bytes_outside = (uint64_t)(a->meshBytesOutside);
        out->area.pool_size = (uint64_t)(a->poolSize);
        out->area.pool_largest_free = (uint64_t)(a->poolLargestFree);
        out->area.engine_mesh_types = (uint64_t)(a->engineMeshTypes);
        out->area.engine_mesh_bytes = (uint64_t)(a->engineMeshBytes);
        out->area.idle_keep = (uint64_t)(a->idleKeep);
        out->area.fixed_used = (uint64_t)(a->fixedUsed);
        out->area.fixed_max = (uint64_t)(a->fixedMax);
        out->area.nonfixed_used = (uint64_t)(a->nonfixedUsed);
        out->area.nonfixed_max = (uint64_t)(a->nonfixedMax);
        out->area.sample_us = (uint64_t)(scaled(a->sampleMs, 1000.0));
    }
    if (f->addr_qpc) {
        out->directory[10].flags = U9TM_GROUP_AVAILABLE;
        out->address_space.collected_qpc = f->addr_qpc;
        out->address_space.free_bytes = (uint64_t)(f->addr_free_bytes);
        out->address_space.largest_free_bytes = (uint64_t)(f->addr_largest_bytes);
        out->address_space.walk_us = (uint64_t)(scaled(f->addr_walk_ms, 1000.0));
    }
    if (f->vram_qpc) {
        out->directory[11].flags = U9TM_GROUP_AVAILABLE;
        out->vram.collected_qpc = f->vram_qpc;
        out->vram.source = (uint64_t)f->vram_kind;
        out->vram.dedicated_bytes = f->vram_dedicated_bytes;
        out->vram.local_usage_bytes = f->vram_local_use_bytes;
        out->vram.local_budget_bytes = f->vram_local_budget_bytes;
        out->vram.shared_usage_bytes = f->vram_shared_use_bytes;
        out->vram.shared_budget_bytes = f->vram_shared_budget_bytes;
        out->vram.query_us = scaled(f->vram_query_ms, 1000.0);
    }
    if (f->tex_list_qpc) {
        const U9TexListItem *t = (const U9TexListItem *)f->tex_list;
        unsigned i, n = f->tex_list_count < U9TM_LIST_ITEMS ? f->tex_list_count : U9TM_LIST_ITEMS;
        out->directory[15].flags = U9TM_GROUP_AVAILABLE;
        out->texture_list.collected_qpc = f->tex_list_qpc;
        out->texture_list.cache_entries = f->tex_list_entries;
        out->texture_list.count = n;
        for (i = 0; i < n; i++) {
            unsigned age = f->tex_list_frame >= t[i].lastUsed ? f->tex_list_frame - t[i].lastUsed : 0;
            out->texture_list.item[i] = (uint64_t)((unsigned)t[i].id & 0xFFFFu) |
                (uint64_t)(t[i].id2 < 0 ? 0u : t[i].id2 > 255 ? 255u : (unsigned)t[i].id2) << 16 |
                (uint64_t)(t[i].w > 4095u ? 4095u : t[i].w) << 24 | (uint64_t)(t[i].h > 4095u ? 4095u : t[i].h) << 36 |
                (uint64_t)(age > 4095u ? 4095u : age) << 48 | (uint64_t)(t[i].overridden ? 1u : 0u) << 60 |
                (uint64_t)(t[i].keyed ? 1u : 0u) << 61;
        }
    }
    if (f->ov_res_qpc) {
        out->directory[12].flags = U9TM_GROUP_AVAILABLE;
        out->override_resident.collected_qpc = f->ov_res_qpc;
        out->override_resident.count = (uint64_t)(f->ov_res_count);
        out->override_resident.bytes = (uint64_t)(f->ov_res_bytes);
    }
}

static void copy_player(const U9TelemetryInputs *in, U9TelemetryGameRecord *out)
{
    const U9PlayerStats *p = in->player;
    U9TelemetryPlayer *g = &out->player;
    if (!p || in->map < 0 || (!p->recordValid && !p->datumsValid)) return;
    out->directory[17].flags = U9TM_GROUP_AVAILABLE;
    g->collected_qpc = in->world_qpc;
    g->weapon = -1;
    if (p->recordValid) {
        g->record_valid = 1;
        g->strength = p->strength; g->dexterity = p->dexterity; g->intelligence = p->intelligence;
        g->health = p->health; g->health_max = p->healthMax; g->health_base = p->healthBase;
        g->mana = p->mana; g->mana_max = p->manaMax; g->mana_base = p->manaBase;
        g->breath = p->breath; g->breath_max = p->breathMax; g->breath_base = p->breathBase;
        g->armour = p->armour; g->armour_bonus = p->armourBonus; g->magic_resist_bonus = p->magicResistBonus;
        g->state_flags = p->stateFlags;
        g->weapon = p->weapon;
        g->skill_fists = p->skill[0]; g->skill_one_handed = p->skill[1]; g->skill_two_handed = p->skill[2];
        g->skill_staff = p->skill[3]; g->skill_ranged = p->skill[4];
        g->spells_lo = p->spellsLo; g->spells_hi = p->spellsHi;
        g->active_spells = p->activeSpells;
        g->spell_discount = (uint64_t)(p->spellDiscount != 0);
        g->combat_states = p->combatStates;
        g->stun_ms = p->stunMs;
        g->equip_left_hand = p->equip[0]; g->equip_right_hand = p->equip[1]; g->equip_scabbard = p->equip[2];
        g->equip_left_shoulder = p->equip[3]; g->equip_right_shoulder = p->equip[4];
        g->equip_quiver = p->equip[5]; g->equip_left_forearm = p->equip[6];
        g->equip_hands_class = p->handsClass;
        g->gold = p->gold; g->arrows = p->arrows;
    }
    if (p->datumsValid) {
        g->datums_valid = 1;
        g->class_chosen = p->classChosen ? 1 : 0;
        g->class_id = (uint64_t)(p->classId & 0xFF);
        g->shrines = p->shrines & 0xFFu;
    }
}

void u9telemetry_metrics_copy(const U9TelemetryInputs *in, U9TelemetryGameRecord *out)
{
    const U9GLMetrics *m = in->metrics;
    const U9MetricsFigures *f = in->figures;
    u9telemetry_game_record_init(out);
    if (!in->available) return;
    out->directory[0].flags = out->directory[1].flags = U9TM_GROUP_AVAILABLE;
    out->frames.collected_qpc = out->resources.collected_qpc = in->now;
    out->frames.frame_no = m->frame_no;
    out->frames.presents = m->presents;
    out->frames.present_failed = m->present_failed;
    out->frames.draw_calls_last = m->draw_calls_last;
    out->frames.vertices_last = m->vertices_last;
    out->resources.textures_alive = m->textures_alive;
    out->resources.texture_bytes = m->texture_bytes;
    out->resources.textures_created = m->textures_created;
    out->resources.textures_destroyed = m->textures_destroyed;
    if (in->memory_qpc) {
        out->directory[2].flags = out->directory[8].flags = U9TM_GROUP_AVAILABLE;
        out->memory.collected_qpc = out->memory_trend.collected_qpc = in->memory_qpc;
        out->memory.private_bytes = m->private_bytes;
        out->memory.working_set = m->working_set;
        out->memory.peak_working_set = m->peak_working_set;
        out->memory.pagefile = m->pagefile;
        out->memory_trend.floor_slope_milli = scaled_signed(f->floor_slope, 1000.0);
        out->memory_trend.raw_slope_milli = scaled_signed(f->raw_slope, 1000.0);
        out->memory_trend.span_frames = f->slope_span_frames;
        out->memory_trend.window_samples = f->window_samples;
        out->memory_trend.window_capacity = f->window_capacity;
        out->memory_trend.floor_rising = f->floor_rising ? 1 : 0;
    }

    if (f->tick_qpc) {
        out->directory[3].flags = out->directory[6].flags = out->directory[7].flags = U9TM_GROUP_AVAILABLE;
        out->pacing.collected_qpc = out->texture_cache.collected_qpc = out->overrides.collected_qpc = f->tick_qpc;
        out->pacing.fps_milli = scaled(f->fps, 1000.0);
        out->texture_cache.cached = f->tex_cached;
        out->texture_cache.cache_bytes = f->tex_bytes;
        out->texture_cache.budget_bytes = f->tex_budget_bytes;
        out->texture_cache.pending = f->tex_pending;
        out->texture_cache.uploads = f->tex_uploads;
        out->texture_cache.duplicates = f->tex_dupes;
        out->texture_cache.evictions = f->tex_evictions;
        out->texture_cache.uploads_per_sec = f->tex_uploads_per_sec;
        out->texture_cache.evictions_per_sec = f->tex_evictions_per_sec;
        out->texture_cache.upload_max_us = scaled(f->upload_ms_max, 1000.0);
        out->overrides.enabled = f->ov_enabled ? 1 : 0;
        out->overrides.files = f->ov_files;
        out->overrides.used = f->ov_used;
        out->overrides.rejected = f->ov_rejected;
        out->overrides.load_max_us = scaled(f->ov_load_ms_max, 1000.0);
    }

    copy_heavy(in, out);
    if (in->gpu_support == 2 || (in->gpu_have && in->gpu_issued_qpc)) {
        out->directory[13].flags = U9TM_GROUP_AVAILABLE;
        out->gpu.collected_qpc = in->gpu_have ? in->gpu_issued_qpc : in->now;
        out->gpu.frame_no = in->gpu_frame;
        out->gpu.samples = in->gpu_samples;
        out->gpu.skipped = in->gpu_skipped;
        out->gpu.support = (uint64_t)in->gpu_support;
        out->gpu.frame_us = scaled(in->gpu_frame_ms, 1000.0);
        out->gpu.region_mask = in->gpu_mask;
        out->gpu.scene_us = scaled(in->gpu_region_ms[0], 1000.0);
        out->gpu.beam_us = scaled(in->gpu_region_ms[1], 1000.0);
        out->gpu.rays_us = scaled(in->gpu_region_ms[2], 1000.0);
        out->gpu.bloom_us = scaled(in->gpu_region_ms[3], 1000.0);
        out->gpu.ao_us = scaled(in->gpu_region_ms[4], 1000.0);
        out->gpu.pool_shadows_us = scaled(in->gpu_region_ms[5], 1000.0);
        out->gpu.sun_shadows_us = scaled(in->gpu_region_ms[6], 1000.0);
        out->gpu.water_us = scaled(in->gpu_region_ms[7], 1000.0);
        out->gpu.composite_us = scaled(in->gpu_region_ms[8], 1000.0);
    }
    out->directory[4].flags = U9TM_GROUP_AVAILABLE;
    out->renderer.collected_qpc = in->now;
    out->renderer.mip_mode = (uint64_t)(in->mip_mode < 0 ? 0 : in->mip_mode);
    out->renderer.anisotropy_milli = scaled(in->anisotropy, 1000.0);
    out->renderer.depth_mode = (uint64_t)(in->depth_mode < 0 ? 0 : in->depth_mode);
    out->renderer.buffers_alive = m->buffers_alive;
    out->renderer.buffer_bytes = m->buffer_bytes;
    out->renderer.fbos_alive = m->fbos_alive;
    if (!in->world_qpc) return;
    copy_player(in, out);
    out->directory[5].flags = U9TM_GROUP_AVAILABLE;
    out->world.collected_qpc = in->world_qpc;
    if (in->map >= 0) { out->world.map_valid = 1; out->world.map = (uint64_t)in->map; }
    if (in->avatar_valid) {
        out->world.avatar_valid = 1;
        out->world.avatar_x = in->avatar_x;
        out->world.avatar_y = in->avatar_y;
        out->world.avatar_z = in->avatar_z;
    }
}
