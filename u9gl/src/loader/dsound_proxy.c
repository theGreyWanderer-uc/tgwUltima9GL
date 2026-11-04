#include <windows.h>
#include <string.h>

#include "u9gl_version.h"

__attribute__((used)) static const char g_marker[] = "u9gl dsound loader " U9GL_VERSION;

#define DSERR_GENERIC_HR  ((HRESULT)0x80004005L)
#define CLASS_E_NOTAVAIL  ((HRESULT)0x80040111L)

static HRESULT WINAPI f_Create(void *a, void *b, void *c)               { (void)a;(void)b;(void)c; return DSERR_GENERIC_HR; }
static HRESULT WINAPI f_Enumerate(void *a, void *b)                     { (void)a;(void)b; return DSERR_GENERIC_HR; }
static HRESULT WINAPI f_CanUnloadNow(void)                              { return S_FALSE; }
static HRESULT WINAPI f_GetClassObject(void *a, void *b, void *c)       { (void)a;(void)b;(void)c; return CLASS_E_NOTAVAIL; }
static HRESULT WINAPI f_GetDeviceID(void *a, void *b)                   { (void)a;(void)b; return DSERR_GENERIC_HR; }
static HRESULT WINAPI f_FullDuplex(void *a, void *b, void *c, void *d, void *e,
                                   void *f, void *g, void *h, void *i, void *j)
{ (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j; return DSERR_GENERIC_HR; }

#define PROXY(name, fallback)                                                   \
    __attribute__((used)) static FARPROC r_##name = (FARPROC)fallback;          \
    __attribute__((naked)) void u9p_##name(void)                                \
    {                                                                            \
        __asm__("jmp *_r_" #name);                                               \
    }

PROXY(DirectSoundCreate,            f_Create)
PROXY(DirectSoundEnumerateA,        f_Enumerate)
PROXY(DirectSoundEnumerateW,        f_Enumerate)
PROXY(DllCanUnloadNow,              f_CanUnloadNow)
PROXY(DllGetClassObject,            f_GetClassObject)
PROXY(DirectSoundCaptureCreate,     f_Create)
PROXY(DirectSoundCaptureEnumerateA, f_Enumerate)
PROXY(DirectSoundCaptureEnumerateW, f_Enumerate)
PROXY(GetDeviceID,                  f_GetDeviceID)
PROXY(DirectSoundFullDuplexCreate,  f_FullDuplex)
PROXY(DirectSoundCreate8,           f_Create)
PROXY(DirectSoundCaptureCreate8,    f_Create)

static void take(HMODULE real, const char *name, FARPROC *slot)
{
    FARPROC p = GetProcAddress(real, name);
    if (p) *slot = p;
}

static void bind_real_dsound(void)
{
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, sizeof(path));
    HMODULE real;

    if (!n || n + sizeof("\\dsound.dll") > sizeof(path)) return;
    lstrcatA(path, "\\dsound.dll");
    real = LoadLibraryA(path);
    if (!real) {
        OutputDebugStringA("u9gl loader: system dsound.dll did not load - no sound\n");
        return;
    }
    take(real, "DirectSoundCreate",            &r_DirectSoundCreate);
    take(real, "DirectSoundEnumerateA",        &r_DirectSoundEnumerateA);
    take(real, "DirectSoundEnumerateW",        &r_DirectSoundEnumerateW);
    take(real, "DllCanUnloadNow",              &r_DllCanUnloadNow);
    take(real, "DllGetClassObject",            &r_DllGetClassObject);
    take(real, "DirectSoundCaptureCreate",     &r_DirectSoundCaptureCreate);
    take(real, "DirectSoundCaptureEnumerateA", &r_DirectSoundCaptureEnumerateA);
    take(real, "DirectSoundCaptureEnumerateW", &r_DirectSoundCaptureEnumerateW);
    take(real, "GetDeviceID",                  &r_GetDeviceID);
    take(real, "DirectSoundFullDuplexCreate",  &r_DirectSoundFullDuplexCreate);
    take(real, "DirectSoundCreate8",           &r_DirectSoundCreate8);
    take(real, "DirectSoundCaptureCreate8",    &r_DirectSoundCaptureCreate8);
}

static void trim(char *s)
{
    char *a = s, *b;
    while (*a == ' ' || *a == '\t') a++;
    if (a != s) memmove(s, a, strlen(a) + 1);
    b = s + strlen(s);
    while (b > s && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r')) *--b = 0;
}

static void read_loader_keys(const char *iniPath, int *enabled, char *dll, int dllSize)
{
    HANDLE f = CreateFileA(iniPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD size, got, i;
    char *buf, line[256];
    unsigned n = 0;

    if (f == INVALID_HANDLE_VALUE) return;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) { CloseHandle(f); return; }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (!buf) { CloseHandle(f); return; }
    if (!ReadFile(f, buf, size, &got, NULL)) got = 0;
    buf[got] = 0;
    CloseHandle(f);

    for (i = 0; i <= got; i++) {
        if (buf[i] != '\n' && buf[i] != 0) {
            if (n < sizeof(line) - 1) line[n++] = buf[i];
            continue;
        }
        line[n] = 0;
        n = 0;
        trim(line);
        if (line[0] && line[0] != '#' && line[0] != ';') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                trim(line);
                trim(eq + 1);
                if (lstrcmpiA(line, "loader") == 0)
                    *enabled = (eq[1] != '0');
                else if (lstrcmpiA(line, "loader_dll") == 0 && eq[1])
                    lstrcpynA(dll, eq + 1, dllSize);
            }
        }
        if (buf[i] == 0) break;
    }
    HeapFree(GetProcessHeap(), 0, buf);
}

static int join(char *out, int size, const char *dir, const char *name)
{
    if (lstrlenA(dir) + lstrlenA(name) >= size) return 0;
    lstrcpynA(out, dir, size);
    lstrcatA(out, name);
    return 1;
}

static int exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void load_ours(HINSTANCE self)
{
    char dir[MAX_PATH], sub[MAX_PATH], path[MAX_PATH], dll[64] = "u9log.dll", env[8];
    const char *home;
    char *slash;
    int enabled = 1;

    if (GetEnvironmentVariableA("U9GL_LOADER", env, sizeof(env)) > 0 && env[0] == '0')
        return;
    if (!GetModuleFileNameA(self, dir, sizeof(dir))) return;
    slash = strrchr(dir, '\\');
    if (!slash) return;
    slash[1] = 0;

    home = dir;
    if (join(sub, sizeof(sub), dir, "u9gl\\") &&
        ((join(path, sizeof(path), sub, "u9gl.ini") && exists(path)) ||
         (join(path, sizeof(path), sub, "u9log.dll") && exists(path))))
        home = sub;

    if (join(path, sizeof(path), home, "u9gl.ini"))
        read_loader_keys(path, &enabled, dll, sizeof(dll));
    if (!enabled) return;
    if (strchr(dll, '\\') || strchr(dll, '/') || strchr(dll, ':')) {
        OutputDebugStringA("u9gl loader: loader_dll must be a file name, not a path\n");
        return;
    }

    if (!join(path, sizeof(path), home, dll)) return;
    if (!exists(path)) {
        OutputDebugStringA("u9gl loader: our DLL was not found - the game runs stock\n");
        return;
    }
    if (!LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))
        OutputDebugStringA("u9gl loader: our DLL failed to load - the game runs stock\n");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        bind_real_dsound();
        load_ours(inst);
    }
    return TRUE;
}
