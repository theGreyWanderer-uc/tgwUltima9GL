#ifndef U9_ADDRESSES_H
#define U9_ADDRESSES_H

#define U9_IMAGE_BASE            0x00400000u

#define U9_VTBL_RENDERER_BASE    0x0074F840u
#define U9_VTBL_RENDERER_DX6     0x0074FA70u
#define U9_VTBL_RENDERER_DX7     0x0074FBB8u
#define U9_VTBL_RENDERER_GLIDE   0x0074FD10u
#define U9_VTBL_SLOT_COUNT       55u

#define U9_VTBL_RENDERER_OWNER   0x0074EF30u
#define U9_OWNER_SLOT_COUNT      8u
#define U9_SLOT_CREATE_FOR_RAST  0
#define U9_FACTORY_ADDR          0x0055E780u
#define U9_OFF_OWNER_RENDERER    0x04u

#define U9_OP_NEW                0x007108F0u
#define U9_GLIDE_CTOR            0x0059F320u
#define U9_OWNER_PREINIT         0x0055EA30u
#define U9_RENDERER_BIND_OWNER   0x00585B70u
#define U9_GLIDE_OBJECT_SIZE     0x450570u

#define U9_RENDERER_BASE_CTOR    0x00585A80u
#define U9_PURECALL              0x00711440u
#define U9_OFF_LIST_HEAD         0x450148u
#define U9_GLIDE_RANGE_LO        0x0059F000u
#define U9_GLIDE_RANGE_HI        0x005AE000u

#define U9_RENDERER_BIND_MATERIAL 0x00585C90u
#define U9_RENDERER_SET_MATERIAL 0x00586550u
#define U9_OFF_VIEW              0x10u
#define U9_CAM_PROJ_SX           0x08u
#define U9_CAM_PROJ_SY           0x0Cu
#define U9_CAM_TO_WORLD          0x74u
#define U9_CAM_CENTRE_X          0xA4u
#define U9_CAM_CENTRE_Y          0xA8u
#define U9_OFF_LINE_COLOUR       0x3Cu
#define U9_OFF_DRAW_MODE         0x40u
#define U9_OFF_BUCKETS           0x4971Cu
#define U9_OFF_LIST_COUNT        0x49CB8u
#define U9_OFF_BUCKET_SCALE      0x49CCCu
#define U9_OFF_BUCKET_INDEX      0x49CD0u
#define U9_OFF_DRAW_LATCH        0x49CE4u
#define U9_OFF_VERTEX_POOL       0x49CE8u
#define U9_OFF_BATCH_RECORDS     0xED9Cu
#define U9_OFF_SCRATCH_VERTS     0x450150u
#define U9_OFF_TMU_COUNT         0x450420u
#define U9_R5_MAX_BATCH_RECORDS  10000
#define U9_R5_MAX_POOL_VERTS     45000
#define U9_R5_MAX_LIST_NODES     20000
#define U9_VTBL_FOG_GLIDE        0x007458C8u
#define U9_FOG_VTBL_SLOTS        14u
#define U9_FOGOBJ_SUSPENDED      0xFD4u

#define U9_OFF_OWNER_ARG5        0x114u
#define U9_OFF_OWNER_154         0x154u
#define U9_OFF_OWNER_168         0x168u
#define U9_OFF_OWNER_16C         0x16Cu
#define U9_OFF_OWNER_170         0x170u
#define U9_OFF_ARG5_448          0x448u

#define U9_RASTERIZER_VALUE      0x00990A98u
#define U9_STAT_TRIANGLES        0x0098C760u
#define U9_STAT_VERTICES         0x0098C764u
#define U9_STAT_CLIPPED          0x0098C768u
#define U9_G_GLIDE_API           0x00AFC2C0u
#define U9_G_GLIDE_HWND          0x00AFC390u

#define U9_OFF_WIDTH             0x000044u
#define U9_OFF_HEIGHT            0x000048u
#define U9_OFF_BPP               0x00004Cu
#define U9_OFF_DEPTH_WRITE       0x049CE0u
#define U9_OFF_STATE_LATCH       0x049CE4u
#define U9_OFF_GLIDE_STATE2      0x45056Cu
#define U9_CREATEDEVICE_BPP      16u
#define U9_OFF_WSCALE            0x49CC8u
#define U9_OFF_BATCH_COUNT       0x49CBCu
#define U9_OFF_POOL_VERTS        0x49CC0u
#define U9_OFF_CUR_MATERIAL      0x049CD4u
#define U9_OFF_PRIM_FLAGS        0x049CDCu
#define U9_OFF_DISABLED_GUARD    0x450564u
#define U9_OFF_STATE_SHADOW      0x450568u
#define U9_OFF_TEXMGR            0x450560u

#define U9_TEXMGR_CURRENT        0x7D6Cu

#define U9_TEX_GET_SOURCE        0x005AD010u

#define U9_G_BITMAP_MGR          0x0084F7F4u
#define U9_BITMAP_GET_DESC       0x0041DCB0u
#define U9_BMDESC_FMT_SELECT     0x01u
#define U9_BMDESC_MIP_COUNT      0x08u
#define U9_BMDESC_IS_16BIT       0x0Au
#define U9_BMDESC_VALID          0x0Fu
#define U9_BMDESC_DIM_A          0x14u
#define U9_BMDESC_DIM_B          0x18u

#define U9_TEXFRAME_HDR_FIXED    0x14u
#define U9_TEXFRAME_16BIT_BIT    0x400u

#define U9_TEXSRC_ARRAY          0x40u
#define U9_TEXSRC_MAX_ID         0x1F40u
#define U9_TEXSRC_SLOT_STRIDE    0x18u
#define U9_TEXSRC_DATA           0x0Cu

#define U9_TEX_ARCHIVE           0x0084F7F4u

#define U9_TEXUI_SLOTS           0x63270u
#define U9_TEXUI_ID_MIN          0x1F40u
#define U9_TEXUI_ID_MAX          0x2340u
#define U9_TEXUI_PENDING_LIST    0x69F6Cu
#define U9_TEXUI_PENDING_COUNT   0x6AF6Cu
#define U9_TEX_COMPOSED_ALLOC    0x0041DE40u
#define U9_TEX_COMPOSED_FREE     0x0041DEC0u
#define U9_TEX_COMPOSED_DRAIN    0x0041DF90u
#define U9_TEX_LOAD_BUFFER       0x0041D9E0u
#define U9_TEX_LOAD_UI           0x0041D930u
#define U9_ENGINE_FREE           0x007108E0u

#define U9_INIT_ZERO_A_OFF       0x49CE8u
#define U9_INIT_ZERO_A_DWORDS    0xA4CB8u
#define U9_INIT_ZERO_B_OFF       0x2DCFC8u
#define U9_INIT_ZERO_B_DWORDS    0x493E0u
#define U9_FOG_ALLOC_BYTES       0x1018u
#define U9_GLIDE_FOG_CTOR        0x00494F00u
#define U9_GLIDE_FOG_DTOR        0x00494500u
#define U9_DTOR_ARRAY_OFF        0x401F48u
#define U9_DTOR_ARRAY_COUNT      0x4E20u
#define U9_DTOR_ARRAY_STRIDE     0x10u
#define U9_ARRAY_DESTROY         0x00710E70u
#define U9_ARRAY_ELEM_DTOR       0x0059F3B0u
#define U9_VTBL_RENDERER_BASE    0x0074F840u
#define U9_GLIDE_TEXCACHE_TEARDOWN 0x005ABD40u

#define U9_GLIDE_BODY_LO         0x0059F000u
#define U9_GLIDE_BODY_HI         0x005AC000u

#define U9_OFF_SLOT04_A          0x000050u
#define U9_OFF_SLOT04_B          0x049CC8u
#define U9_SLOT04_SCALE          0x0074FDF4u
#define U9_OFF_SLOT14_BYTE       0x000068u
#define U9_SHARED_SLOT13_TAIL    0x00586820u
#define U9_TEXSRC_STAMP          0x10u
#define U9_TEXSRC_SIZE           0x14u

#define U9_G_TEX_PALETTE         0x00AFBEA0u
#define U9_PAL_TRANSPARENT       254u
#define U9_PAL_ENTRIES           256u
#define U9_TEXMGR_BUCKET0        0x7D70u
#define U9_TEXMGR_BUCKET_STRIDE  0x34u

#define U9_TEXREC_NEXT           0x00u
#define U9_TEXREC_PREV           0x04u
#define U9_TEXREC_ID             0x10u
#define U9_TEXREC_ID2            0x12u
#define U9_TEXREC_STAMP          0x14u
#define U9_TEXREC_SMALL_LOD      0x18u
#define U9_TEXREC_LARGE_LOD      0x1Cu
#define U9_TEXREC_ASPECT         0x20u
#define U9_TEXREC_FORMAT         0x24u
#define U9_TEXREC_DATA           0x28u
#define U9_TEXREC_TMU            0x2Cu
#define U9_TEXREC_START_ADDR     0x30u
#define U9_TEXREC_SIZE           0x34u

#define U9_MAT_TEXID2            0x0Au
#define U9_MAT_LOG2W             0x0Cu
#define U9_MAT_LOG2H             0x0Eu
#define U9_MAT_TEXW              0x24u
#define U9_MAT_TEXH              0x28u

#define GRV_FLOATS   15
#define GRV_X         0
#define GRV_Y         1
#define GRV_Z         2
#define GRV_R         3
#define GRV_G         4
#define GRV_B         5
#define GRV_OOZ       6
#define GRV_A         7
#define GRV_OOW       8
#define GRV_TMU0_SOW  9
#define GRV_TMU0_TOW 10
#define GRV_TMU0_OOW 11

#define U9_MAT_TEXID             0x08u
#define U9_MAT_FLAGS             0x10u
#define U9_MAT_CHROMAKEY         0x14u

#define U9_MATF_TRANSLUCENT      0x002u
#define U9_MATF_ALPHATEST_B      0x004u
#define U9_MATF_FMT_SELECT       0x020u
#define U9_MATF_CHROMA_MASK      0x030u
#define U9_MATF_CHROMA_ON        0x010u
#define U9_MATF_ADDITIVE         0x400u
#define U9_MATF_CLAMP_S          0x080u
#define U9_MATF_CLAMP_T          0x100u

#define U9_OFF_FOG_OBJECT        0x0Cu
#define U9_FOGOBJ_COLOUR         0x04u
#define U9_CLEAR_NO_FOG          0xFF808080u

#define U9_FOGOBJ_ENABLED        0x08u
#define U9_FOGOBJ_MODE           0x0Cu
#define U9_FOGOBJ_TABLE          0x0FD8u
#define U9_FOG_TABLE_SIZE        64u

#define U9_OFF_FOG_ENABLE        0x29u
#define U9_OFF_FOG_FAR           0x2Cu
#define U9_OFF_FOG_NEAR          0x30u
#define U9_OFF_FOG_RANGE         0x34u

#define U9_GLIDE_SLOT0_DTOR      0x0059F3F0u
#define U9_GLIDE_SLOT32_DRAWTRI  0x005A0840u

#define U9_AREA_LIST_OWNER       0x00AD127Cu
#define U9_AREA_LIST_HEAD        0x48u
#define U9_AREACELL_SLOTS        0x10u
#define U9_AREACELL_FREEHEAD     0x14u
#define U9_AREACELL_COUNT        0x18u
#define U9_AREACELL_NEXT         0x20u
#define U9_AREA_SLOT_STRIDE      0x10u
#define U9_AREA_SLOTS_USABLE     2045u
#define U9_AREA_SLOTS_ALLOC      2046u

#define U9_AREA_RINGS            0x20u
#define U9_AREA_RING_STRIDE      0x0Cu
#define U9_AREA_CELLS            0x44u
#define U9_AREA_GRID_W           0x4Cu
#define U9_AREA_GRID_H           0x50u
#define U9_AREA_LRU_COUNT        0x58u
#define U9_AREA_TMESH_COUNT_A    0x70u
#define U9_AREA_TMESH_COUNT_B    0x88u
#define U9_AREA_TMESH_MAX        0xA0u
#define U9_AREA_QUEUES           0xB8u
#define U9_AREACELL_STRIDE       0x28u
#define U9_AREACELL_FLAGS        0x1Cu
#define U9_LIST_HEAD             0x04u
#define U9_LINK_NEXT             0x04u
#define U9_QLINK_MESHES          0x1Cu
#define U9_QLINK_INSTANCES       0x20u
#define U9_SLOT_INSTANCE         0x04u
#define U9_SLOT_OBJECT           0x08u
#define U9_SLOT_MOVER            0x0Cu
#define U9_CUR_ITEM_MGR          0x008515E8u
#define U9_ITEMMGR_FIXED         0x10u
#define U9_ITEMMGR_NONFIXED      0x34u
#define U9_OBJ_TYPE              0x0Au
#define U9_MESH_TABLE            0x00926DF0u
#define U9_MESH_ENTRY            12u
#define U9_MESH_ENTRIES          8000u
#define U9_MESH_SIZES            0x0091F0F0u
#define U9_MESH_POOL             0x0098C758u
#define U9_MESH_IDLE_KEEP        0x0098FBD0u
#define U9_STAT_IN_CAMERA        0x0098C780u
#define U9_STAT_MESH_TYPES       0x0098C798u
#define U9_STAT_MESH_BYTES       0x0098C79Cu

#define U9_HANDLE_TABLE          0x008515D0u
#define U9_HANDLE_STRIDE         0x0Cu
#define U9_HANDLE_OBJ            0x08u
#define U9_OBJ_TYPEID            0x0Au

#define U9_SLOT_DTOR              0
#define U9_SLOT_INITIALIZE        1
#define U9_SLOT_CREATE_DEVICE     7
#define U9_SLOT_CLEAR            15
#define U9_SLOT_TEST_AND_DRAW    21
#define U9_SLOT_DRAW_TRIANGLE    32
#define U9_SLOT_DRAW_LINE        31
#define U9_SLOT_DRAW_POLY_LIST   33
#define U9_SLOT_DRAW_POLYGON     34
#define U9_SLOT_FLUSH_SELECTED   35
#define U9_SLOT_FLUSH_BUCKET_A   36
#define U9_SLOT_FLUSH_LIST_POOL  37
#define U9_SLOT_FLUSH_SORTED     38
#define U9_SLOT_FLUSH_BUCKET_B   39
#define U9_SLOT_FLUSH_LINES      40
#define U9_SLOT_PRESENT          42
#define U9_SLOT_SET_DEPTH_WRITE  45
#define U9_SLOT_SET_RENDER_STATE 48
#define U9_SLOT_04                4
#define U9_SLOT_05                5
#define U9_SLOT_06                6
#define U9_SLOT_SET_DEPTH_MODE   43
#define U9_SLOT_SET_DEPTH_FUNC   46
#define U9_SLOT_SET_GAMMA         9
#define U9_SLOT_GET_GAMMA        10
#define U9_SLOT_SET_MIPMAP       47

#define U9_OFF_MIP_ENABLED       0x5Cu
#define U9_OFF_MIP_LODBIAS       0x60u
#define U9_OFF_MIP_LEVEL         0x64u
#define U9_TEXMGR_MIP_ENABLED    0x39u
#define U9_TEXMGR_MIP_LODBIAS    0x3Cu
#define U9_MIP_MIN_LOG2          5
#define U9_SLOT_SET_MATERIAL_P   11
#define U9_SLOT_SET_MATERIAL_I   12

#define U9_SKY                   0x013206D8u
#define U9_SKY_TIME              0x00u
#define U9_SKY_UNDERGROUND       0x1Cu
#define U9_SKY_MOON_MAP          0x20u
#define U9_SKY_SUN_DIR           0x28u
#define U9_SKY_STORM             0x6Cu

#define U9_SKY_MOON_INDEX_A      0x40u
#define U9_SKY_MOON_INDEX_B      0x42u
#define U9_SKY_MOON_HALO_A       0x413Cu
#define U9_SKY_MOON_HALO_B       0x4140u
#define U9_SKYOBJ_POS            0x17Cu
#define U9_SKYOBJ_ALPHA          0x140u
#define U9_WORLDCLOCK_PTR        0x0083D3B0u
#define U9_CLOCK_TICKS           0xA8u
#define U9_CLOCK_DAY             0xACu
#define U9_WORLD_PTR             0x00850228u
#define U9_WORLD_CAMERA          0xBCu
#define U9_CAMERA_POS            0xC4u

#endif
