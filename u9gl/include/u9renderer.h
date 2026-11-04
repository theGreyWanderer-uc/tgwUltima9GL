#ifndef U9RENDERER_H
#define U9RENDERER_H

#include <stdint.h>

typedef uint32_t u32;
typedef uint8_t  u8;
typedef int16_t  s16;

#if defined(_MSC_VER)
#  define U9CALL __thiscall
#elif defined(__GNUC__) && defined(__i386__)
#  define U9CALL __attribute__((thiscall))
#else
#  error "U9 renderer slots require 32-bit x86 thiscall; build with MinGW i686 GCC (u9gl/build.sh)."
#endif

typedef struct U9Vertex {
    float x;
    float z;
    float y;
    float r, g, b;
    float a;
    float r2, g2, b2;
    float flag28;
    float s, t;
} U9Vertex;

typedef struct U9Material {
    u8   pad00[0x08];
    s16  texId;
    s16  texId2;
    s16  log2w;
    s16  log2h;
    u32  flags;
    u32  chromaKey;
    u8   pad18[0x0C];
    u32  texW;
    u32  texH;
} U9Material;

typedef struct GrVertexU9 {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    struct { float sow, tow, oow; } tmuvtx[2];
} GrVertexU9;

typedef struct U9SortNode {
    struct U9SortNode *left;
    struct U9SortNode *right;
    float  depth;
    void  *texture;
    u32    count;
    void  *verts;
} U9SortNode;

typedef struct U9BatchNode {
    u32   unknown0;
    struct U9BatchNode *next;
    u32   count;
    void *verts;
} U9BatchNode;

enum U9PrimFlags {
    U9_PRIM_COLOUR    = 0x01,
    U9_PRIM_BLEND     = 0x02,
    U9_PRIM_CONSTA    = 0x04,
    U9_PRIM_MODEHI    = 0x08,
    U9_PRIM_SECONDARY = 0x10,
    U9_PRIM_TEXTURED  = 0x20,
    U9_PRIM_BIT6      = 0x40,
    U9_PRIM_TRANSLUCENT = 0x0E
};
#define U9_PRIM_INITIAL 0x41

enum U9FlushBits {
    U9_FLUSH_BUCKET_A = 0x01,
    U9_FLUSH_SORTED   = 0x02,
    U9_FLUSH_LINES    = 0x04,
    U9_FLUSH_BUCKET_B = 0x08
};

enum U9MaterialFlags {
    U9_MAT_MODEMASK  = 0x00C,
    U9_MAT_LODBIAS   = 0x020,
    U9_MAT_GATE40    = 0x040,
    U9_MAT_CLAMP_S   = 0x080,
    U9_MAT_CLAMP_T   = 0x100,
    U9_MAT_CLAMP_ST  = 0x180,
    U9_MAT_POINT_FILTER = 0x200
};

enum U9ClipBits {
    U9_CLIP_XMIN = 0x01,
    U9_CLIP_XMAX = 0x02,
    U9_CLIP_ZMIN = 0x04,
    U9_CLIP_ZMAX = 0x08,
    U9_CLIP_YMIN = 0x10,
    U9_CLIP_YMAX = 0x20
};

#define U9_ALPHA_PER_VERTEX (-1.0f)
#define U9_ALPHA_OVERRIDE_MIN (0.0f)

#define U9_MAX_POOL_VERTS    44999
#define U9_MAX_BATCH_RECORDS 10000
#define U9_MAX_RETAINED      20000
#define U9_DEPTH_BUCKETS     32

#define U9_DEPTH_SCALE_NUM   31.0f
#define U9_SUBPIXEL          0.0625f

struct U9Renderer;

typedef struct U9RendererVtbl {
     void  (U9CALL *dtor)(struct U9Renderer*, u32 deleting);
     u8    (U9CALL *Initialize)(struct U9Renderer*, u32 width,
                          u32 height, u32 bpp);
     void *shared_02;
     void *shared_03;
     void  (U9CALL *slot04)(struct U9Renderer*, float);
     void *shared_05;
     void *shared_06;
     void  (U9CALL *CreateDevice)(struct U9Renderer*, u32 width, u32 height,
                          u32 reqBpp);
     void *shared_08_14[7];
     void  (U9CALL *Clear)(struct U9Renderer*, u32 alpha, u32 depth);
     void  (U9CALL *slot16)(struct U9Renderer*);
     void  (U9CALL *slot17)(struct U9Renderer*);
     void *shared_18_20[3];
     u8    (U9CALL *TestAndDraw)(struct U9Renderer*,
                          const U9Vertex*, const U9Vertex*, const U9Vertex*, u32 flags);
     void *shared_22_30[9];
     void  (U9CALL *DrawLine)(struct U9Renderer*, const U9Vertex *v0,
                          const U9Vertex *v1, u32 packedColour);
     void  (U9CALL *DrawTriangle)(struct U9Renderer*,
                          const U9Vertex*, const U9Vertex*, const U9Vertex*, float alpha);
     void  (U9CALL *DrawPolygonList)(struct U9Renderer*, u32 count,
                          U9Vertex *verts, float alphaOverride, u32 project);
     void  (U9CALL *DrawPolygon)(struct U9Renderer*, u32 count,
                          GrVertexU9 *verts);
     void  (U9CALL *FlushSelected)(struct U9Renderer*, u32 mask);
     void  (U9CALL *FlushBucketPoolA)(struct U9Renderer*);
     void  (U9CALL *FlushListPool)(struct U9Renderer*);
     void  (U9CALL *FlushSortedBatches)(struct U9Renderer*);
     void  (U9CALL *FlushBucketPoolB)(struct U9Renderer*);
     void  (U9CALL *FlushLines)(struct U9Renderer*);
     void  (U9CALL *SortedInsert)(struct U9Renderer*, void **bucketRoot,
                          U9SortNode *node);
     void  (U9CALL *Present)(struct U9Renderer*);
     void  (U9CALL *SetDepthMode)(struct U9Renderer*, u32);
     void  (U9CALL *DisableDepthBuffer)(struct U9Renderer*);
     void  (U9CALL *SetDepthWrite)(struct U9Renderer*, u32);
     void  (U9CALL *SetDepthFunc)(struct U9Renderer*, u32);
     void  (U9CALL *SetMipMapLevel)(struct U9Renderer*, u32 level0to3);
     void  (U9CALL *SetRenderState)(struct U9Renderer*, u32);
     u32   (U9CALL *slot49)(struct U9Renderer*, u32, u32);
     void  (U9CALL *ScreenshotToFile)(struct U9Renderer*, const char *path);
     void* (U9CALL *CaptureScreen)(struct U9Renderer*, u32 toBuffer);
     void  (U9CALL *Shutdown)(struct U9Renderer*);
     void  (U9CALL *RestoreState)(struct U9Renderer*);
} U9RendererVtbl;

typedef struct U9Renderer { const U9RendererVtbl *vtbl; } U9Renderer;

#endif
