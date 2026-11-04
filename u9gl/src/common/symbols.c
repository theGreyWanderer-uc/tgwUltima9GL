#include <stdint.h>
#include "symbols.h"
#include "buildid.h"

typedef struct {
    uintptr_t   addr_1_19F;
    const char *name;
} SymRow;

static const SymRow TABLE[U9SYM_COUNT] = {
    [U9SYM_GR_DRAW_TRIANGLE]      = { 0u, "glide2x!grDrawTriangle" },
    [U9SYM_GR_DRAW_POLYGON]       = { 0u, "glide2x!grDrawPolygon" },
    [U9SYM_GR_DRAW_POLY_VLIST]    = { 0u, "glide2x!grDrawPolygonVertexList" },
    [U9SYM_GR_DRAW_PLANAR_POLY]   = { 0u, "glide2x!grDrawPlanarPolygon" },
    [U9SYM_GR_DRAW_PLANAR_VLIST]  = { 0u, "glide2x!grDrawPlanarPolygonVertexList" },
    [U9SYM_GR_DRAW_LINE]          = { 0u, "glide2x!grDrawLine" },
    [U9SYM_GR_DRAW_POINT]         = { 0u, "glide2x!grDrawPoint" },
    [U9SYM_GR_AA_DRAW_TRIANGLE]   = { 0u, "glide2x!grAADrawTriangle" },
    [U9SYM_GR_AA_DRAW_POLYGON]    = { 0u, "glide2x!grAADrawPolygon" },
    [U9SYM_GR_AA_DRAW_POLY_VLIST] = { 0u, "glide2x!grAADrawPolygonVertexList" },
    [U9SYM_GR_AA_DRAW_LINE]       = { 0u, "glide2x!grAADrawLine" },
    [U9SYM_GR_AA_DRAW_POINT]      = { 0u, "glide2x!grAADrawPoint" },
    [U9SYM_GR_TEX_DL_MIPMAP]      = { 0u, "glide2x!grTexDownloadMipMap" },
    [U9SYM_GR_TEX_DL_LEVEL]       = { 0u, "glide2x!grTexDownloadMipMapLevel" },
    [U9SYM_GR_TEX_DL_PARTIAL]     = { 0u, "glide2x!grTexDownloadMipMapLevelPartial" },
    [U9SYM_GR_TEX_DL_TABLE]       = { 0u, "glide2x!grTexDownloadTable" },
    [U9SYM_GR_TEX_DL_TABLE_PART]  = { 0u, "glide2x!grTexDownloadTablePartial" },
    [U9SYM_GR_SILENCE]            = { 0u, "glide2x!<silenced export>" },

    [U9SYM_ACTOR_TICK_PRUNE_SLOT_A] =
        { 0x00749058u, "Actor_Tick_PruneRefs slot (vtable 0x00748F08)" },
    [U9SYM_ACTOR_TICK_PRUNE_SLOT_B] =
        { 0x00747F78u, "Actor_Tick_PruneRefs slot (vtable 0x00747C50)" },

    [U9SYM_ACTOR_SCHEDULE_REBUILD_SLOT] =
        { 0x00748FECu, "Actor_ScheduleRebuild slot 57 (+0xE4)" },
    [U9SYM_ACTOR_REBUILD_STIM_SLOT] =
        { 0x007490DCu, "Actor_RebuildStimulusTables slot 117 (+0x1D4)" },

    [U9SYM_ACTOR_NEW_CALL_BASE] =
        { 0x004F6250u, "Actor_Create: CALL operator new, base actor" },
    [U9SYM_ACTOR_NEW_CALL_DERIVED] =
        { 0x004F6842u, "Actor_Create: CALL operator new, derived actor" },
    [U9SYM_ACTOR_CTOR_CALL_BASE] =
        { 0x004F626Fu, "Actor_Create: CALL the base actor constructor" },
    [U9SYM_ACTOR_CTOR_CALL_DERIVED] =
        { 0x004F6861u, "Actor_Create: CALL the derived actor constructor" },
    [U9SYM_SPELL_KIND_1] =
        { 0x005EC57Fu, "spell-type test in FUN_005EC560: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EC57E)" },
    [U9SYM_SPELL_KIND_2] =
        { 0x005EC5E6u, "spell-type test in the sweep before 0x005EC650 (slot 21): the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EC5E5)" },
    [U9SYM_SPELL_KIND_3] =
        { 0x005EC691u, "spell-type test in the sweep at 0x005EC650 (slot 22) - F-19's: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EC690)" },
    [U9SYM_SPELL_KIND_4] =
        { 0x005EC8D2u, "spell-type test in FUN_005EC8B0: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EC8D1)" },
    [U9SYM_SPELL_KIND_5] =
        { 0x005ECC2Au, "spell-type test in FUN_005ECBE0: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005ECC29)" },
    [U9SYM_SPELL_KIND_6] =
        { 0x005ED62Cu, "spell-type test in FUN_005ED5F0: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005ED62B)" },
    [U9SYM_SPELL_KIND_7] =
        { 0x005ED6CEu, "spell-type test in FUN_005ED670: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005ED6CD)" },
    [U9SYM_SPELL_KIND_8] =
        { 0x005EE0DEu, "spell-type test in SpellMgr_StartSpell: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EE0DD)" },
    [U9SYM_SPELL_KIND_9] =
        { 0x005EE238u, "spell-type test in SpellMgr_StartSpell: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EE237)" },
    [U9SYM_SPELL_KIND_10] =
        { 0x005EE495u, "spell-type test in SpellMgr_StartSpell: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005EE494)" },
    [U9SYM_SPELL_KIND_11] =
        { 0x005F16A4u, "spell-type test in FUN_005F1610: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005F16A3)" },
    [U9SYM_SPELL_KIND_12] =
        { 0x005F17CAu, "spell-type test in FUN_005F1760: the 0x9A of CMP EAX,0x9A (3D 9A 00 00 00 at 0x005F17C9)" },
    [U9SYM_RESET_TRANSIENT_RELOAD_IMM] =
        { 0x0043CB62u, "Actor_ResetTransientState: the +0x178 reload constant" },

    [U9SYM_TELEPORT_NULL_A] =
        { 0x0064975Du, "SpellTeleport_Tick: MOV EDX,[EBP+0xE8], unchecked null" },
    [U9SYM_TELEPORT_NULL_B] =
        { 0x00649747u, "SpellTeleport_Tick: CALL with an unchecked this" },
    [U9SYM_TELEPORT_RESUME_A] =
        { 0x00649763u, "SpellTeleport_Tick: resume after the displaced insn" },
    [U9SYM_TELEPORT_BAIL_END] =
        { 0x0064985Cu, "SpellTeleport_Tick: teardown, slot 0x2C then return" },
    [U9SYM_TELEPORT_BAIL_RETURN] =
        { 0x00649863u, "SpellTeleport_Tick: plain epilogue" },
    [U9SYM_TELEPORT_TOLERANCE_PUSH] =
        { 0x00649763u, "SpellTeleport_Tick state 5: PUSH 0 / LEA EAX,[ESP+0x60] / PUSH 0, the move's last two arguments" },
    [U9SYM_TELEPORT_TOLERANCE_RESUME] =
        { 0x0064976Bu, "SpellTeleport_Tick state 5: MOV EBX,[EDX] after the pushes" },
    [U9SYM_BLINK_SPOT_RETURN] =
        { 0x004DD914u, "Caster_PickBlinkSpot epilogue: MOV EAX,ESI / POP ESI / ADD ESP,0x34, then RET 4" },
    [U9SYM_BLINK_SPOT_PICKED] =
        { 0x004AE5DCu, "Caster_BeginSpell after CALL [vtbl+0x1E4]: PUSH EAX / LEA ECX,[ESP+0x24]" },
    [U9SYM_BLINK_SPOT_PICKED_RESUME] =
        { 0x004AE5E1u, "Caster_BeginSpell: the CALL that copies the picked spot" },
    [U9SYM_BLINK_SPOT_PICKED_B] =
        { 0x004DD565u, "Caster_BeginSpellChecked after CALL [vtbl+0x1E4]: PUSH EAX / LEA ECX,[ESP+0x20]" },
    [U9SYM_BLINK_SPOT_PICKED_B_RESUME] =
        { 0x004DD56Au, "Caster_BeginSpellChecked: the CALL that copies the picked spot" },
    [U9SYM_BLINK_PRECAST_TEST] =
        { 0x004DD58Eu, "Caster_BeginSpellChecked: TEST EAX,EAX / JZ 0x004DD724 on Mover_PlaceNear's test" },
    [U9SYM_BLINK_PRECAST_RESUME] =
        { 0x004DD596u, "Caster_BeginSpellChecked: the cast goes ahead" },
    [U9SYM_BLINK_PRECAST_BAIL] =
        { 0x004DD724u, "Caster_BeginSpellChecked: the cast is dropped" },
    [U9SYM_CAST_CHECKED] =
        { 0x004DD490u, "Caster_BeginSpellChecked entry, PUSH -1 / PUSH 0x0072CF98" },
    [U9SYM_CAST_PLAIN] =
        { 0x004AE3B0u, "Caster_BeginSpell entry, PUSH -1 / PUSH 0x0072B8C0" },
    [U9SYM_CASTER_APPROACH] =
        { 0x004DD3B4u, "Caster_Idle: MOV EAX,[ESI+0x104], the approach call's first instruction" },
    [U9SYM_CASTER_APPROACH_RESUME] =
        { 0x004DD3BAu, "Caster_Idle: MOV EDX,[ESI], the approach call continues" },
    [U9SYM_CASTER_APPROACH_SKIP] =
        { 0x004DD3CDu, "Caster_Idle: POP ESI / POP ECX / RET, past the approach" },
    [U9SYM_BLINK_DISTANCE] =
        { 0x004DD862u, "Caster_PickBlinkSpot: MOV EDX,80 / MOV ECX,66, the Rand_Range bounds" },
    [U9SYM_CASTER_PAUSE_SET] =
        { 0x004DD7A5u, "the caster spell-finished callback: MOV [ESI+0x2B8],EAX, the post-spell pause" },
    [U9SYM_CASTER_PAUSE_RESUME] =
        { 0x004DD7ABu, "the caster spell-finished callback: MOV EAX,[ESI+0xC] after it" },
    [U9SYM_GOLD_CAP_HEADROOM] =
        { 0x004AD435u, "Gold_Headroom_9999: MOV ECX,0x270F, the cap every headroom check reads" },
    [U9SYM_GOLD_CAP_PICKUP] =
        { 0x004AD5C1u, "Gold_PickUpPile: MOV EDI,0x270F, the pickup's own copy of the cap" },
    [U9SYM_GOLDWIDGET_REDRAW_SLOT] =
        { 0x00753698u, "vtable 0x00753640 slot 22 (+0x58): GoldWidget_NeedsRedraw 0x005CC680" },
    [U9SYM_GOLD_TOTAL] =
        { 0x004AD2F0u, "Gold_Total: no arguments, plain RET; the Avatar's gold - called" },
    [U9SYM_ITEM_CREATE] =
        { 0x00413810u, "Item_Create: fastcall (ECX out-handle, EDX type), location and flags, RET 8 - called" },
    [U9SYM_HANDLE_SETPROP] =
        { 0x004C56F0u, "set a property: thiscall on the handle's address, (property, value), RET 8 - called" },
    [U9SYM_HANDLE_ADDREF] =
        { 0x004C60A0u, "a handle's reference count +1: stdcall (handle), RET 4 - called" },
    [U9SYM_HANDLE_RELEASE] =
        { 0x004C3B90u, "a handle's reference count -1, slot freed at 0: stdcall (handle), RET 4 - called" },
    [U9SYM_LOOT_LAYOUT_DROP] =
        { 0x00546410u, "Loot_LayOutAndDrop: stdcall (handle, x, y, z), RET 0x10; releases the handle - called" },
    [U9SYM_MOVER_GETYAW] =
        { 0x0057CCD0u, "Mover_GetYaw, slot +0xCC of vtable 0x00749C00: FLD [ECX+0x1A8] - compared against" },
    [U9SYM_MOVER_PLACENEAR] =
        { 0x0057D330u, "Mover_PlaceNear, slot +0xE8 of vtable 0x00749C00: thiscall (&dest float[3], facing, testOnly), RET 0xC - called through the slot when it holds this" },
    [U9SYM_ACTOR_SET_COMBAT_TARGET] =
        { 0x004F9530u, "Actor_SetCombatTarget: thiscall on the actor table base, (sourceObject, targetObject), RET 8 - called" },
    [U9SYM_ACTOR_PATH_TO_POINT] =
        { 0x0043D400u, "Actor_PathToPoint: thiscall (actor, x, y, z floats by value, stopDist (-1 none), speed, wait, moveIfFailed), RET 0x1C; base-actor fields only - called" },
    [U9SYM_READBOOK_PATH_CALL] =
        { 0x00408A2Au, "CALL Activity_PathToPoint (0x00401230) in ReadBook_Step state 0: (bookX, bookY, 100.0, 0), E8 01 88 FF FF - patched (readbook standoff)" },
    [U9SYM_ITEMICON_COLLIDE_SLOT] =
        { 0x007536E0u, "vtbl_ItemIcon 0x007536A0 slot 16 (+0x40): Widget_Collide 0x005C5670" },
    [U9SYM_CAPACITY_BACKPACK] =
        { 0x007856ECu, "g_containerCapacities 0x007856E8, type 0 (the Avatar): i32 capacity 20" },
    [U9SYM_CAPACITY_BAG] =
        { 0x007856F4u, "g_containerCapacities, type 2342 (the bag): i32 capacity 9" },
    [U9SYM_GRID_BACKPACK] =
        { 0x007A1A30u, "g_containerLayouts 0x007A1A20 row 0 (backpack) +0x10: i32 rows 4, cols 5" },
    [U9SYM_GRID_BAG] =
        { 0x007A1AC0u, "g_containerLayouts row 3 (bag) +0x10: i32 rows 3, cols 3" },
    [U9SYM_BAG_PLACE_SLOT] =
        { 0x007534CCu, "vtbl_ContainerWindow 0x00753448 slot 33 (+0x84): ContainerWindow_PlaceItem 0x005CA480, thiscall 5 args, RET 0x14" },
    [U9SYM_BACKPACK_PLACE_SLOT] =
        { 0x00753574u, "vtbl_BackpackWindow 0x007534F0 slot 33 (+0x84): ContainerWindow_PlaceItem 0x005CA480 (inherited)" },
    [U9SYM_BAG_FILL_SLOT] =
        { 0x007534E0u, "vtbl_ContainerWindow slot 38 (+0x98): ContainerWindow_FillCells 0x005C9A10, this only" },
    [U9SYM_BACKPACK_FILL_SLOT] =
        { 0x00753588u, "vtbl_BackpackWindow slot 38 (+0x98): BackpackWindow_FillCells 0x005CAB60, this only" },
    [U9SYM_WIDGET_MOVETO] =
        { 0x005C5100u, "Widget_MoveTo: thiscall (widget, x, y), RET 8 - called" },
    [U9SYM_ITEM_FIRST_CONTENT] =
        { 0x004BF250u, "Item_FirstContent: thiscall (obj), plain RET; first contained object or 0 - called" },
    [U9SYM_ITEM_NEXT_CONTENT] =
        { 0x004BF320u, "Item_NextContent: thiscall (obj), plain RET; next object in its container or 0 - called" },
    [U9SYM_CASTER_APPROACH_AT] =
        { 0x004BB6D0u, "slot +0xD0 of vtable 0x00748F08: approach a point (x,y,z by value, dist, speed, wait, moveIfFailed), RET 0x1C - called, not patched" },
    [U9SYM_TELEPORT_TICK] =
        { 0x00649350u, "SpellTeleport_Tick entry, MOV EAX,FS:[0]" },
    [U9SYM_TELEPORT_MOVE_CALL] =
        { 0x0064979Eu, "SpellTeleport_Tick state 5: CALL [EBX+0xE4], the move" },
    [U9SYM_TELEPORT_MOVE_RESUME] =
        { 0x006497A4u, "SpellTeleport_Tick state 5: after the move" },
    [U9SYM_FASTREF_RELEASE] =
        { 0x004CD120u, "FastRef_Release entry, PUSH EBP / MOV EBP,ESP / SUB ESP,0x10" },
    [U9SYM_FASTREF_RELEASE_RESUME] =
        { 0x004CD126u, "FastRef_Release after its prologue" },
    [U9SYM_PAGE_FREECELL] =
        { 0x004D1C30u, "Page_FreeCell entry, MOV EDX,[ESP+4] / PUSH ESI" },
    [U9SYM_PAGE_FREECELL_RESUME] =
        { 0x004D1C35u, "Page_FreeCell after the displaced instructions" },
    [U9SYM_MOVER_QUERY_DONE] =
        { 0x0057CF17u, "Mover_PlaceAt after the slot +0x180 ground query, MOV ECX,[ESI+0xD8]" },
    [U9SYM_MOVER_QUERY_RESUME] =
        { 0x0057CF1Du, "Mover_PlaceAt at the +0xD8 bit 8 test" },
    [U9SYM_PATHFINDER_TICK] =
        { 0x005030E0u, "Pathfinder_Tick entry, MOV EAX,FS:[0] - vtable 0x0074B128 slot 1" },
    [U9SYM_PATHFINDER_AIM] =
        { 0x00502C80u, "Pathfinder_AimAtTarget: thiscall (Vec3 *out, target object), RET 8; reads only - called" },
    [U9SYM_ITEM_USE] =
        { 0x004BEE70u, "Item_Use entry, MOV EAX,FS:[0]: thiscall (obj, user obj), RET 4; queues usecode event 3" },
    [U9SYM_BAG_TOGGLE] =
        { 0x00658880u, "BagWindow_Toggle entry, MOV EAX,FS:[0]: thiscall (this, char open), RET 4; item handle at this+0xD8" },
    [U9SYM_CONTAINER_CLOSEFOR] =
        { 0x005C9240u, "ContainerWindow_CloseFor entry, PUSH -1 / PUSH 0x0073294C: stdcall (item handle), RET 4" },
    [U9SYM_CONTAINER_CLOSE_SLOT] =
        { 0x00753494u, "vtbl_ContainerWindow 0x00753448 slot 19 (+0x4C): ContainerWindow_Close 0x005C93C0" },
    [U9SYM_BACKPACK_CLOSE_SLOT] =
        { 0x0075353Cu, "vtbl_BackpackWindow 0x007534F0 slot 19 (+0x4C): ContainerWindow_Close 0x005C93C0" },
    [U9SYM_SOUND_PLAY_FOR_OBJECT] =
        { 0x00537D40u, "Sound_PlayForObject entry, PUSH -1 / PUSH 0x0072FE98: thiscall on g_Sound (handle, category byte, 0), RET 0xC" },
    [U9SYM_DOOR_CLOSE_STEP_SLOT] =
        { 0x00755774u, "door close process vtable 0x00755770 slot 1 (+0x04): DoorClose_Step 0x0063C310, thiscall, RET" },
    [U9SYM_DOOR_OPEN_STEP_SLOT] =
        { 0x007557CCu, "door open process vtable 0x007557C8 slot 1 (+0x04): DoorOpen_Step 0x0063C8B0, thiscall, RET" },
    [U9SYM_DOOR_USE] =
        { 0x0063B8E0u, "Door_Use entry, MOV EAX,FS:[0]: thiscall on the usecode object (handle +0), RET; event 3 of door types 1096-1098" },
    [U9SYM_DOOR_DRIVER] =
        { 0x0063BD30u, "door driver entry, PUSH -1 / PUSH 0x00736539: thiscall on the usecode object (handle +0, mode +0x0C), RET" },
    [U9SYM_SPACES_ASSIGN_OBJECT] =
        { 0x0053C5B0u, "Spaces_AssignObject entry, PUSH ECX / PUSH EBX / PUSH EBP / MOV EBP,[ESP+0x10]: thiscall on the space manager (scene record, world object), RET 8; doors re-test their portals here" },
    [U9SYM_SPACES_RESET_PORTALS] =
        { 0x0053BE50u, "Spaces_ResetPortals entry, SUB ESP,0xC / PUSH EBX / MOV [ESP+0xC],ECX: fastcall on the space manager, RET; zeroes every portal's flags; only caller Spaces_TrackCamera" },
    [U9SYM_LIGHTS_SAVE_CALL] =
        { 0x0051EEF3u, "SaveLoad_SaveFile: CALL 0x004DC570 Lights_Save, MOV ECX,0x008515F0 / PUSH file: thiscall (file), RET 4; writes the light system's +0x64 (ranged lights on) into the save" },
    [U9SYM_LIGHTS_LOAD_CALL] =
        { 0x00521BF9u, "SaveLoad_LoadFile: CALL 0x004DC890 Lights_Load, MOV ECX,0x008515F0 / PUSH file: thiscall (file), RET 4; resets the light system (0x004D68C0, +0x64 = 1) and reads +0x64 back from the save" },
    [U9SYM_LIGHTSYS_MARK_ALL_DIRTY] =
        { 0x004D7D50u, "LightSys_MarkAllDirty entry: thiscall on the light system 0x008515F0 (force, flags), RET 8; force clears each element's last-lit stamp, so every object in the active area is relit; the engine calls it (1, 0) at 0x0049E7A4 and 0x004A0075" },
    [U9SYM_CREATURE_SOUND] =
        { 0x0043FD30u, "Creature_Sound entry, MOV EAX,[ESP+4] / PUSH 0: thiscall (category), RET 4; calls Sound_PlayForObject" },
    [U9SYM_FLYER_REFUSED] =
        { 0x00493F40u, "Flyer slot 49 (+0xC4), MOV EAX,[ECX+0x228]: thiscall (a, b), RET 8; sets flag 0x4, the blocked report" },
    [U9SYM_GRID_FLOOR_CALL] =
        { 0x005045B9u, "GridPath_FindStep: CALL 0x0056EAB0, the floor query a grid node is judged by: thiscall on the walker object (slope out, vector out, 0), RET 0xC" },
    [U9SYM_CHASE_LEGGOAL_CALL] =
        { 0x00503C43u, "Pathfinder_Tick case 7: CALL 0x005B37D0, legGoal = lastSeenPos" },
    [U9SYM_APPROACH_PATH_CALL] =
        { 0x004BB6BAu, "Caster_ApproachActor (0x004BB690, combatant slot +0xD4): CALL Actor_PathToActor 0x0043D220, thiscall (enemy, dist, speed, wait, moveIfFailed)" },
    [U9SYM_RAIN_SPACE_CALL] =
        { 0x005BCF5Fu, "Sky_UpdateRain (0x005BCE50): CALL SpaceTree_SpaceAt 0x0053E440, thiscall on the spaces tree (point = the falling drop's position), RET 4; non-zero kills the drop" },
    [U9SYM_SKY_FOG_CALL] =
        { 0x005B92DBu, "Sky_FrameUpdate (0x005B8BA0): CALL Sky_UpdateFog 0x005B97E0, fastcall (sky in ECX), the per-frame weather fog" },
    [U9SYM_LIGHTNING_LIGHT_CALL] =
        { 0x005BD5B7u, "Sky_LightningStrike (0x005BD440): CALL Light_Construct 0x004D5F10, thiscall (r, g, b, position, spread 0.5, range 0, flags 0), RET 0x24 - the strike's light" },
    [U9SYM_LIGHTNING_COLOUR_CALL] =
        { 0x005BD9FFu, "Sky_UpdateLightning (0x005BD8D0): CALL Light_SetColour 0x004D5FF0, thiscall (light, r, g, b), RET 0xC - the strike's light faded each frame" },
    [U9SYM_LIGHTNING_BOLT_CALL] =
        { 0x005BD84Au, "Sky_LightningStrike (0x005BD440): CALL 0x0060CAC0, fastcall (effect 3 = the storm's bolt, 0) + 0x48 bytes, RET 0x48 - the bolt effect, with its own light" },
    [U9SYM_LIGHTNING_ADD_CALL] =
        { 0x005BD5D1u, "Sky_LightningStrike (0x005BD440): CALL LightSys_AddLight 0x004D6BE0, thiscall on 0x008515F0 (light, 0), RET 8 - the strike's light added" },
    [U9SYM_LIGHTNING_REMOVE_CALL] =
        { 0x005BD9CDu, "Sky_UpdateLightning (0x005BD8D0): CALL LightSys_RemoveLight 0x004D7050, thiscall on 0x008515F0 (light, 0), RET 8 - searches both lists, frees the light only if found" },
    [U9SYM_LIGHT_DTOR] =
        { 0x004D5FD0u, "the light's destructor: thiscall (light), plain RET - called, as LightSys_RemoveLight does before the delete" },
    [U9SYM_ENGINE_FREE] =
        { 0x007108E0u, "the engine's operator delete: cdecl (pointer) - called" },
    [U9SYM_LIGHTNING_SCRIPT_CALL] =
        { 0x004E579Cu, "a script command's case (switch ending 0x004E591D): CALL 0x0060CAC0 with effect 3, the storm's bolt, at an object's position" },
    [U9SYM_LIGHTNING_STORMOBJ_CALL] =
        { 0x0062FCF3u, "FUN_0062FB70 (from 0x00630024, beside the object code that writes sky+0x7C): CALL 0x0060CAC0 with effect 3, the storm's bolt" },
    [U9SYM_SPACE_AT] =
        { 0x0053E440u, "SpaceTree_SpaceAt: thiscall (spaces tree, point), RET 4; the space holding the point or 0 - called" },
    [U9SYM_CAMERA_PLANES_CALL] =
        { 0x0055ECB4u, "Camera_SetFOV (0x0055EC20): its last call, CALL 0x0055EEA0 (rebuild the view planes), after the angle, view distance and vfov are stored" },
    [U9SYM_BOOK_SETFOV_CALL] =
        { 0x00422959u, "Book_Construct (0x00422580): CALL Camera_SetFOV with 60.0f on the game camera, after saving its angle at book +0x1C0" },
    [U9SYM_CAMERA_FIXPLANES] =
        { 0x0055EEA0u, "Camera_RebuildPlanes: thiscall (camera), plain RET; frustum from +0x08/+0x0C and the half-sizes - called" },
    [U9SYM_CAMERA_SIZE_PLANES_CALL] =
        { 0x0055EB5Cu, "Camera_SetSize (0x0055EA30): CALL 0x0055EEA0 after the view distance is recomputed from the stored angle" },
    [U9SYM_CAMERA_LOAD_PLANES_CALL] =
        { 0x0056369Bu, "Camera_Load (0x005630B0): CALL 0x0055EEA0 after the saved angle is read and the view distance recomputed" },
    [U9SYM_CAMERA_FOV2_PLANES_CALL] =
        { 0x0055ED93u, "Camera_SetFOV2 (0x0055ECC0, thiscall(camera, h, v)): CALL 0x0055EEA0 after +0x08 from h and +0x0C from v" },
    [U9SYM_SPELLDAMAGE_NPCDATA_CALL] =
        { 0x005F0BBBu, "SpellMgr_GetSpellDamage (0x005F0B70): CALL Object_NpcData 0x004BF6A0, fastcall (ECX = the caster object), result read at +0x30 (intelligence) unchecked" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 0] =
        { 0x007558F4u, "spell vtable 0x007558F0 (SpellDeath) slot 1 (+0x04), its tick 0x0063E130" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 1] =
        { 0x0075595Cu, "spell vtable 0x00755958 (ItemInvis) slot 1 (+0x04), its tick 0x0063EB10" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 2] =
        { 0x00755A1Cu, "spell vtable 0x00755A18 (SpellRingOfFire) slot 1 (+0x04), its tick 0x0063FB00" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 3] =
        { 0x00755A84u, "spell vtable 0x00755A80 (SpellManaBreath) slot 1 (+0x04), its tick 0x0063FF50" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 4] =
        { 0x00755AECu, "spell vtable 0x00755AE8 (spell type 0x73) slot 1 (+0x04), its tick 0x00640B10" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 5] =
        { 0x00755B74u, "spell vtable 0x00755B70 (SpellLightningBolt) slot 1 (+0x04), its tick 0x00641180" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 6] =
        { 0x00755C34u, "spell vtable 0x00755C30 (ItemInfernalArmor) slot 1 (+0x04), its tick 0x00642530" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 7] =
        { 0x00755C9Cu, "spell vtable 0x00755C98 (SpellCure) slot 1 (+0x04), its tick 0x00642D30" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 8] =
        { 0x00755D04u, "spell vtable 0x00755D00 (SpellStone) slot 1 (+0x04), its tick 0x00643550" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 9] =
        { 0x00755DDCu, "spell vtable 0x00755DD8 (SpellGust) slot 1 (+0x04), its tick 0x00644680" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 10] =
        { 0x00755E44u, "spell vtable 0x00755E40 (SpellIgnite) slot 1 (+0x04), its tick 0x00644C60" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 11] =
        { 0x00755EACu, "spell vtable 0x00755EA8 (SpellDouse) slot 1 (+0x04), its tick 0x006453A0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 12] =
        { 0x00755F14u, "spell vtable 0x00755F10 (SpellBigStone) slot 1 (+0x04), its tick 0x00645950" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 13] =
        { 0x00755F7Cu, "spell vtable 0x00755F78 (SpellArmageddon) slot 1 (+0x04), its tick 0x00645FE0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 14] =
        { 0x00755FE4u, "spell vtable 0x00755FE0 (SpellEarthquake) slot 1 (+0x04), its tick 0x00646440" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 15] =
        { 0x0075604Cu, "spell vtable 0x00756048 (SpellLightningStorm) slot 1 (+0x04), its tick 0x00646E60" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 16] =
        { 0x007560C4u, "spell vtable 0x007560C0 (SpellInferno) slot 1 (+0x04), its tick 0x006476B0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 17] =
        { 0x0075612Cu, "spell vtable 0x00756128 (SpellAcidRain) slot 1 (+0x04), its tick 0x00648070" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 18] =
        { 0x00756194u, "spell vtable 0x00756190 (SpellFailure) slot 1 (+0x04), its tick 0x006486F0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 19] =
        { 0x007561FCu, "spell vtable 0x007561F8 (SpellTimeStop) slot 1 (+0x04), its tick 0x00648C40" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 20] =
        { 0x00756264u, "spell vtable 0x00756260 (SpellTeleport) slot 1 (+0x04), its tick 0x00649350" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 21] =
        { 0x007562D4u, "spell vtable 0x007562D0 (SpellSummonDeamon) slot 1 (+0x04), its tick 0x00649DC0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 22] =
        { 0x0075633Cu, "spell vtable 0x00756338 (SpellFrostStorm) slot 1 (+0x04), its tick 0x0064A390" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 23] =
        { 0x007563A4u, "spell vtable 0x007563A0 (SpellSummonUndead) slot 1 (+0x04), its tick 0x0064ABF0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 24] =
        { 0x0075640Cu, "spell vtable 0x00756408 (SpellLevitate) slot 1 (+0x04), its tick 0x0064B3C0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 25] =
        { 0x00756474u, "spell vtable 0x00756470 (SpellFlameThrower) slot 1 (+0x04), its tick 0x0064B6E0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 26] =
        { 0x00756534u, "spell vtable 0x00756530 (SpellFullHeal) slot 1 (+0x04), its tick 0x0064BEB0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 27] =
        { 0x0075659Cu, "spell vtable 0x00756598 (SpellCharm) slot 1 (+0x04), its tick 0x0064C990" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 28] =
        { 0x00756604u, "spell vtable 0x00756600 (SpellEtherealSight) slot 1 (+0x04), its tick 0x0064CFC0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 29] =
        { 0x0075666Cu, "spell vtable 0x00756668 (SpellDay) slot 1 (+0x04), its tick 0x0064D860" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 30] =
        { 0x007566D4u, "spell vtable 0x007566D0 (SpellFreeze) slot 1 (+0x04), its tick 0x0064DD60" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 31] =
        { 0x0075673Cu, "spell vtable 0x00756738 (SpellMeteorite) slot 1 (+0x04), its tick 0x0064E4F0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 32] =
        { 0x007567ACu, "spell vtable 0x007567A8 (SpellWizardEye) slot 1 (+0x04), its tick 0x0064EF50" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 33] =
        { 0x00756814u, "spell vtable 0x00756810 (SpellFireball) slot 1 (+0x04), its tick 0x0064F840" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 34] =
        { 0x0075687Cu, "spell vtable 0x00756878 (SpellFog) slot 1 (+0x04), its tick 0x0064FE90" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 35] =
        { 0x007568E4u, "spell vtable 0x007568E0 (SpellCreateReagents) slot 1 (+0x04), its tick 0x00650BD0" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 36] =
        { 0x00756964u, "spell vtable 0x00756960 (SpellTelekinesis) slot 1 (+0x04), its tick 0x00651180" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 37] =
        { 0x007569CCu, "spell vtable 0x007569C8 (spell type 0x71) slot 1 (+0x04), its tick 0x00651A20" },
    [U9SYM_SPELL_TICK_SLOT_FIRST + 38] =
        { 0x00756A34u, "spell vtable 0x00756A30 (SpellLightHeal) slot 1 (+0x04), its tick 0x006522E0" },
    [U9SYM_COMBATANT_CALLBACK_10] =
        { 0x0043BAE0u, "combatant callback, table 0x0078F640 entry 10: fastcall (ecx, edx, item handle, ...), RET 0x10; reads the item's type at +0x0A unchecked, then actor slot +0xC0" },
    [U9SYM_COMBATANT_CALLBACK_11] =
        { 0x0043BBA0u, "combatant callback, table 0x0078F640 entry 11: fastcall (ecx, edx, item handle), RET 0x4; reads the item's type at +0x0A unchecked, then actor slot +0xC4" },
    [U9SYM_COMBATANT_CALLBACK_12] =
        { 0x0043BC60u, "combatant callback, table 0x0078F640 entry 12: fastcall (ecx, edx, item handle), RET 0x4; reads the item's type at +0x0A unchecked, then actor slot +0xBC" },
    [U9SYM_COMBATANT_CALLBACK_13] =
        { 0x0043BD00u, "combatant callback, table 0x0078F640 entry 13: fastcall (ecx, edx, item handle, ...), RET 0xC; reads the item's type at +0x0A unchecked, then actor slot +0xB0" },
    [U9SYM_COMBATANT_CALLBACK_14] =
        { 0x0043BD90u, "combatant callback, table 0x0078F640 entry 14: fastcall (ecx, edx, item handle, ...), RET 0xC; reads the item's type at +0x0A unchecked, then actor slot +0xB4" },
    [U9SYM_COMBATANT_CALLBACK_15] =
        { 0x0043BE20u, "combatant callback, table 0x0078F640 entry 15: fastcall (ecx, edx, item handle, ...), RET 0x8; reads the item's type at +0x0A unchecked, then actor slot +0xB8" },
    [U9SYM_COMBATANT_CALLBACK_30] =
        { 0x0043BF60u, "combatant callback, table 0x0078F640 entry 30: fastcall (ecx, edx, item handle), RET 0x4; reads the item's type at +0x0A unchecked, then actor slot +0xCC" },
    [U9SYM_TELEPORT_CALL_TARGET] =
        { 0x004BE730u, "the callee of the second F-5 site" },
    [U9SYM_REFS_INVALIDATE] =
        { 0x0055B040u, "object-reference invalidator (F-7 faults here)" },
    [U9SYM_REFS_INVALIDATE_CALLER] =
        { 0x005F22C0u, "the caller that reaches F-7's invalidator" },

    [U9SYM_MOVIE_PLAY_CALL_A] =
        { 0x0054FBD7u, "CALL PlayMovie 0x00585390, from U9Trigger_Execute" },
    [U9SYM_MOVIE_PLAY_CALL_B] =
        { 0x00554071u, "CALL PlayMovie 0x00585390, at 0x00554071" },
    [U9SYM_MOVIE_PLAY_CALL_C] =
        { 0x00585536u, "CALL PlayMovie 0x00585390, index 1, no callback" },
    [U9SYM_MOVIE_PLAY_CALL_D] =
        { 0x00646054u, "CALL PlayMovie 0x00585390, index 6 or 7" },
    [U9SYM_MOVIE_NEXT_CALL_A] =
        { 0x00584A08u, "CALL Movie_PlayNext 0x00584A70, from the movie start" },
    [U9SYM_MOVIE_NEXT_CALL_B] =
        { 0x0058512Fu, "CALL Movie_PlayNext 0x00584A70, chain method 0x00585100" },
    [U9SYM_MOVIE_NEXT_CALL_C] =
        { 0x00585484u, "CALL Movie_PlayNext 0x00584A70, chain in PlayMovie" },
    [U9SYM_MOVIE_STOP_CALL_A] =
        { 0x005849F4u, "CALL Movie_FullStop 0x00584E40, movie start path" },
    [U9SYM_MOVIE_STOP_CALL_B] =
        { 0x00584A61u, "CALL Movie_FullStop 0x00584E40, after a MessageBox" },
    [U9SYM_MOVIE_STOP_CALL_KEY] =
        { 0x0058516Cu, "CALL Movie_FullStop 0x00584E40, Space/ESC/Enter" },
    [U9SYM_MOVIE_STOP_CALL_EVENT] =
        { 0x00585189u, "CALL Movie_FullStop 0x00584E40, event type 2" },

    [U9SYM_OWNER_FACTORY_SLOT] =
        { 0x0074EF30u, "vtbl_RendererOwner slot 0 -> Renderer_CreateForRasterizer 0x0055E780" },

    [U9SYM_IAT_GETTICKCOUNT] =
        { 0x00740110u, "u9.exe import slot: KERNEL32!GetTickCount (211 callers)" },
    [U9SYM_IAT_TIMEGETTIME] =
        { 0x00740400u, "u9.exe import slot: WINMM!timeGetTime (the game tick, the UI)" },
    [U9SYM_IAT_PEEKMESSAGEA] =
        { 0x00740374u, "u9.exe import slot: USER32!PeekMessageA (the message loop 0x005D6F70)" },
    [U9SYM_IAT_GETMESSAGEA] =
        { 0x00740378u, "u9.exe import slot: USER32!GetMessageA (the message loop 0x005D6F70)" },

    [U9SYM_MAP_LOAD] =
        { 0x004DF8D0u, "Map_Load: thiscall, 1 stack arg (RET 4); this+0x20 is the map; terrain, textures, then ItemMgr_LoadMap" },
    [U9SYM_OPERATOR_NEW] =
        { 0x007108F0u, "operator new: cdecl (size), RET - called" },
    [U9SYM_MAP_GET_MANAGER] =
        { 0x004DF250u, "Map_GetManager: ECX = map, no stack args, RET; loads the map when needed - called" },
    [U9SYM_MAPCHANGER_CONSTRUCT] =
        { 0x0054A310u, "MapChanger_Construct: thiscall (manager, destObject, mapWord, trigger), RET 0x10 - called" },
    [U9SYM_MAPCHANGER_VTABLE] =
        { 0x0074EAA8u, "MapChanger vtable, slot 1 MapChanger_Tick 0x0054A590 - copied" },
    [U9SYM_WORLD_CHANGEMAP] =
        { 0x0049ED20u, "World_ChangeMap: thiscall on [0x00850228], 6 args, RET 0x18 - called" },
    [U9SYM_TERRAIN_HEIGHT] =
        { 0x004E02A0u, "terrain height: thiscall (manager, tileX, tileY), RET 8; tile = world / 128 - called" },
    [U9SYM_SAVELOAD_ENABLE] =
        { 0x00522150u, "SaveLoad_Enable: thiscall on [0x00AD126C], RET - called" },
    [U9SYM_BLIT_ROW_CLAMP_JCC] =
        { 0x005E2292u, "Bitmap_Blit row clamp: JGE rel8 (7D 06) after CMP EDI,[0x01333934] - patched" },

    [U9SYM_MESHREF_SET_5A5D90] =
        { 0x005A5DD4u, "CALL MeshHandle_Set (0x004E3980) in 0x005A5D90: a stack handle - patched (mesh watch)" },
    [U9SYM_MESHREF_SET_5762C0] =
        { 0x005762DCu, "CALL MeshHandle_Set (0x004E3980) in 0x005762C0: a 0x4C part's handle - patched (mesh watch)" },
    [U9SYM_MESHREF_SET_5A5830] =
        { 0x005A5877u, "CALL MeshHandle_Set (0x004E3980) in 0x005A5830: a stack handle - patched (mesh watch)" },
    [U9SYM_MESHREF_QUEUE_57578C] =
        { 0x0057578Cu, "CALL MeshLoad_Queue (0x004E43E0) in 0x00575450 - patched (mesh watch)" },
    [U9SYM_MESHREF_QUEUE_57586A] =
        { 0x0057586Au, "CALL MeshLoad_Queue (0x004E43E0) in 0x00575450 - patched (mesh watch)" },
    [U9SYM_MESHREF_NOW_4116E9] =
        { 0x004116E9u, "CALL MeshLoad_Now (0x004E4570) in Appearance_Get 0x004116C0 - patched (mesh watch)" },
    [U9SYM_MESHREF_NOW_411726] =
        { 0x00411726u, "CALL MeshLoad_Now (0x004E4570) in Appearance_Get 0x004116C0, the fallback to 0 - patched (mesh watch)" },
    [U9SYM_COLLIDER_EXTRA_REF_INC] =
        { 0x00564D1Bu, "Collider_CopyForElement: INC EDI, the surplus mesh reference (47 3B D6) - patched (I-14)" },
    [U9SYM_MESHSTAT_CREATE_56D30E] =
        { 0x0056D30Eu, "CALL Collider_CreateForObject (0x00564810) in 0x0056D2D0: thiscall (shape, object), RET 4 - patched (mesh census)" },
    [U9SYM_MESHSTAT_COPY_564A48] =
        { 0x00564A48u, "CALL Collider_CopyForElement (0x00564B00) in Collider_CreateForObject - patched (mesh census)" },
    [U9SYM_MESHSTAT_COPY_56F422] =
        { 0x0056F422u, "CALL Collider_CopyForElement (0x00564B00) in Collider_LimbShapesForCylinder 0x0056F2F0 (flag 0) - patched (mesh census)" },
    [U9SYM_MESHSTAT_COPY_56F681] =
        { 0x0056F681u, "CALL Collider_CopyForElement (0x00564B00) in Collider_LimbShapesForInstance 0x0056F590 (flag 1) - patched (mesh census)" },
    [U9SYM_MESHSTAT_DESTROY_564DAF] =
        { 0x00564DAFu, "CALL Collider_Destroy (0x00564D70) in itself: a head destroying its element chain - patched (mesh census)" },
    [U9SYM_MESHSTAT_DESTROY_56DC31] =
        { 0x0056DC31u, "CALL Collider_Destroy (0x00564D70) in 0x0056D770 - patched (mesh census)" },
    [U9SYM_MESHSTAT_DESTROY_56F54A] =
        { 0x0056F54Au, "CALL Collider_Destroy (0x00564D70) in Collider_LimbShapesForCylinder 0x0056F2F0, the teardown - patched (mesh census)" },
    [U9SYM_ATTACK_NOTARGET_DRAGON] =
        { 0x004516B2u, "dragon combatant event handler 0x00451630, event 0x34D: MOV EAX,[0x007862C8] (A1 C8 62 78 00), just before it marks the attack and indexes the actor table with +0x104 unchecked - patched (F-15)" },
    [U9SYM_ATTACK_NOTARGET_DRAGON_EXIT] =
        { 0x004518B5u, "dragon combatant event handler: the common epilogue (MOV ECX,[ESP+0xC] / POP EDI / ... / RET 8), same stack as 0x004516B2" },
    [U9SYM_ATTACK_NOTARGET_EB] =
        { 0x00522BA4u, "combatant event handler near 0x00522B80, event 0xEB: MOV EAX,[0x0078FA08] (A1 08 FA 78 00), the same unchecked +0x104 index - patched (F-15)" },
    [U9SYM_ATTACK_NOTARGET_EB_EXIT] =
        { 0x00522CEDu, "that handler's exit after its event-0xEB work: POP EBX / POP EDI / epilogue, same stack as 0x00522BA4" },
    [U9SYM_COMBATANT_FACE_ACTOR] =
        { 0x0043DD60u, "Combatant_FaceActor, base vtable slot +0x70: thiscall (npc), RET 4; table[npc]+0x14 unchecked - hooked (F-16)" },
    [U9SYM_COMBATANT_IS_FACING_ACTOR] =
        { 0x0043DDB0u, "Combatant_IsFacingActor, slot +0x74: thiscall (npc, float tolerance), RET 8, bool; calls slot +0x7C - hooked (F-16)" },
    [U9SYM_COMBATANT_IS_ACTOR_FACING_ME] =
        { 0x0043DE40u, "Combatant_IsActorFacingMe, slot +0x78: thiscall (npc, float tolerance), RET 8, bool; table[npc]+0x14 unchecked - hooked (F-16)" },
    [U9SYM_COMBATANT_ANGLE_TO_ACTOR] =
        { 0x0043DEE0u, "Combatant_AngleToActor, slot +0x7C: thiscall (npc), RET 4, float; table[npc]+0x14 unchecked, F-16 faulted at 0x0043DF09 - hooked" },
    [U9SYM_COMBATANT_ANGLE_FROM_ACTOR] =
        { 0x0043DF60u, "Combatant_AngleFromActor, slot +0x80: thiscall (npc), RET 4, float; table[npc] unchecked - hooked (F-16)" },
    [U9SYM_COMBATANT_DISTANCE_TO_ACTOR] =
        { 0x0043DFE0u, "Combatant_DistanceToActor, slot +0x84: thiscall (npc, a, b), RET 0xC, float; table[npc] unchecked - hooked (F-16)" },
    [U9SYM_COMBATANT_CAN_SEE_ACTOR] =
        { 0x0043E0C0u, "Combatant_CanSeeActor, slot +0x88: thiscall (npc), RET 4, bool; table[npc] unchecked - hooked (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 0] =
        { 0x004175CEu, "tell-my-target site: MOV ECX,[ECX+EAX*4] / MOV EDX,[ECX] (8B 0C 81 8B 11), CALL [EDX+0x30] at +5, JL target at +8 - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 1] =
        { 0x0042F6B3u, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 2] =
        { 0x004A15DAu, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 3] =
        { 0x004A6EB7u, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 4] =
        { 0x004B89F7u, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 5] =
        { 0x005C1A8Cu, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 6] =
        { 0x005C231Cu, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_TARGET_SLOT_FIRST + 7] =
        { 0x005C2E9Cu, "tell-my-target site, same bytes and layout as 0x004175CE - patched (F-16)" },
    [U9SYM_AVATAR_RADIUS_ADD] =
        { 0x004938D8u, "MOV ECX,[EAX] / FADD [ECX+0x3C] (8B 08 D8 41 3C): the Avatar's actor (table[0]) +0x3C added unchecked - patched (F-16)" },
    [U9SYM_TRIGGER_EXECUTE] =
        { 0x0054F850u, "U9Trigger_Execute entry, PUSH -1 / PUSH 0x00730CA7: thiscall on g_triggerSystem (obj, phase, label), RET 0xC" },
    [U9SYM_SHARED_DATUM_SET] =
        { 0x006159F0u, "SharedDatum_Set entry, MOV EAX,[ESP+4] / TEST EAX,EAX: thiscall on the store (index, byte value), RET 8" },
    [U9SYM_SHARED_DATUM_GET] =
        { 0x006159D0u, "SharedDatum_Get entry, MOV EAX,[ESP+4] / TEST EAX,EAX: thiscall on the store (index), RET 4; returns the byte in AL" },
    [U9SYM_WORLD_COLLECT_BY_LINK] =
        { 0x004C28B0u, "World_CollectByLink: thiscall on a 0x3C-byte iterator (out[], max, map manager, int pos[3], half-extent, link), RET 0x18; out[] zero-ended - called" },
    [U9SYM_TRIGGER_SHOW_OBJECT] =
        { 0x0054EC00u, "Trigger_ShowObject: thiscall on g_triggerSystem (kind 10, arg2, obj), RET 0xC; only for an object with +0x14 & 0x40100 - called" },
    [U9SYM_TRIGGER_HIDE_OBJECT] =
        { 0x0054F350u, "Trigger_HideObject: thiscall on g_triggerSystem (kind 9, arg2, obj, deferred arg2), RET 0x10 - called" },
    [U9SYM_HANDLE_ALLOC] =
        { 0x004C3910u, "Handle_Alloc: (obj), RET 4, ECX unused; the object's handle in its map - called" },
    [U9SYM_EFFECT_END_FOR_OBJECT] =
        { 0x0050C2D0u, "Effect_EndForObject: (handle), RET 4; ends the effect processes whose target is that object, releases one reference; trigger opcode 0x10 - called" },
    [U9SYM_WORLD_FIND_BY_TYPE] =
        { 0x004C23F0u, "World_FindObjectsByBaseType: thiscall on a 0x3C-byte iterator (out[], max, map manager, int pos[3], half-extent, type, flags), RET 0x1C; flags & 3 the lists (1 placed, 2 fixed, 3 both), & 4 exact type - called" },
    [U9SYM_OBJECT_GET_PROPERTY] =
        { 0x004BF010u, "Object_GetProperty: thiscall (obj, tag), RET 4; read-only - the trigger executor reads tags 0x3E/0x3B through it - called" },
    [U9SYM_SHRINE_CLEANSE_TICK_SLOT] =
        { 0x007551D4u, "shrine cleansing process vtable 0x007551D0 slot 1 (+0x04): ShrineCleanse_Tick 0x0062FF00, thiscall, RET; stage at +0xD8" },
    [U9SYM_TRIGGER_SPECIAL_ACTION] =
        { 0x00548900u, "Trigger_SpecialAction entry, MOV EAX,FS:[0] (64 A1 00 00 00 00): fastcall (action, target), trigger opcode 0x3D; action 0 starts a shrine cleansing (0x00630EC0)" },
    [U9SYM_OBJECT_FIRST_CONTENT] =
        { 0x004BF250u, "Object_FirstContent: thiscall (container) -> its first item or 0; special action 12 walks the Avatar's pack with it - called" },
    [U9SYM_OBJECT_NEXT_CONTENT] =
        { 0x004BF320u, "Object_NextContent: thiscall (item) -> the next item in its container or 0 - called" },
    [U9SYM_ITEM_TAKE_OUT_TO_WORLD] =
        { 0x004C0680u, "Item_TakeOutToWorld: thiscall (item, 1, location x, y, z by value, 1), RET 0x14 -> the item's object (it may move); as special action 12 calls it - called" },
    [U9SYM_WORLDOBJ_PLACE_AT] =
        { 0x004C3C10u, "WorldObj_PlaceAt: thiscall (&handle, &location {i32 x, i32 y, i16 z}, 0), RET 8; keeps the facing (0x004BECE0) and calls WorldObj_MoveTo - as stage 13 of a cleansing calls it - called" },
};

static uintptr_t RUNTIME[U9SYM_COUNT];

void u9sym_set_runtime(U9Symbol s, uintptr_t addr)
{
    if ((unsigned)s < U9SYM_COUNT) RUNTIME[s] = addr;
}

uintptr_t u9sym(U9Symbol s)
{
    if ((unsigned)s >= U9SYM_COUNT) return 0;
    if (RUNTIME[s]) return RUNTIME[s];
    if (!u9build_is_exact()) return 0;
    return TABLE[s].addr_1_19F;
}

const char *u9sym_name(U9Symbol s)
{
    if ((unsigned)s >= U9SYM_COUNT) return "?";
    return TABLE[s].name ? TABLE[s].name : "?";
}
