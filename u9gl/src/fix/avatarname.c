#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../injection/config.h"
#include "../common/patchmgr.h"
#include "../common/symbols.h"
#include "../diag/log.h"
#include "avatarname.h"

#define NAME_MAX_CHARS 24

static const uint16_t PATCH_NAME[] = { 'G', 'r', 'a', 'n', 'd', 'o', 'r' };
#define PATCH_NAME_LEN 7u

typedef char (__attribute__((fastcall)) *EntryLengthFn)(uint32_t index, uint32_t *length);
typedef char (__attribute__((fastcall)) *ReadEntryFn)(uint32_t index, uint16_t *buffer);

static EntryLengthFn o_length;
static ReadEntryFn   o_read;

static uint16_t g_name[NAME_MAX_CHARS];
static unsigned g_nameLen;
static int      g_on;
static unsigned g_linesRenamed, g_failures;

static int is_letter(uint16_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7);
}

static int name_at(const uint16_t *text, uint32_t len, uint32_t i)
{
    if (i + PATCH_NAME_LEN > len) return 0;
    if (memcmp(text + i, PATCH_NAME, PATCH_NAME_LEN * sizeof(uint16_t)) != 0) return 0;
    if (i > 0 && is_letter(text[i - 1])) return 0;
    if (i + PATCH_NAME_LEN < len && is_letter(text[i + PATCH_NAME_LEN])) return 0;
    return 1;
}

static uint16_t *renamed_entry(uint32_t index, uint32_t *outLength)
{
    uint32_t len = 0, i, n = 0, out;
    uint16_t *text, *result;

    if (o_length(index, &len) != 1 || len == 0 || len > 0x100000u) return NULL;
    text = (uint16_t *)malloc(len * sizeof(uint16_t));
    if (!text) { g_failures++; return NULL; }
    if (o_read(index, text) != 1) { free(text); g_failures++; return NULL; }

    for (i = 0; i < len; i++)
        if (name_at(text, len, i)) { n++; i += PATCH_NAME_LEN - 1; }
    if (n == 0) { free(text); return NULL; }

    out = len + n * g_nameLen - n * PATCH_NAME_LEN;
    result = (uint16_t *)malloc(out * sizeof(uint16_t));
    if (!result) { free(text); g_failures++; return NULL; }
    {
        uint32_t w = 0;
        for (i = 0; i < len; ) {
            if (name_at(text, len, i)) {
                memcpy(result + w, g_name, g_nameLen * sizeof(uint16_t));
                w += g_nameLen;
                i += PATCH_NAME_LEN;
            } else
                result[w++] = text[i++];
        }
    }
    free(text);
    *outLength = out;
    return result;
}

static char __attribute__((fastcall)) h_length(uint32_t index, uint32_t *length)
{
    uint32_t n;
    uint16_t *t;
    char ok = o_length(index, length);
    if (ok != 1 || !g_on) return ok;
    t = renamed_entry(index, &n);
    if (t) { *length = n; free(t); }
    return ok;
}

static char __attribute__((fastcall)) h_read(uint32_t index, uint16_t *buffer)
{
    uint32_t n;
    uint16_t *t;
    if (!g_on) return o_read(index, buffer);
    t = renamed_entry(index, &n);
    if (!t) return o_read(index, buffer);
    memcpy(buffer, t, n * sizeof(uint16_t));
    free(t);
    g_linesRenamed++;
    return 1;
}

static int load_name(void)
{
    const char *s = u9cfg_str("avatar_name", "");
    unsigned len, i;
    while (*s == ' ' || *s == '\t') s++;
    len = (unsigned)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    if (len == 0) return 0;
    if (len > NAME_MAX_CHARS) {
        u9log_line("AVATAR NAME: avatar_name is longer than %d characters - not used", NAME_MAX_CHARS);
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || (c >= 0x7F && c < 0xA0)) {
            u9log_line("AVATAR NAME: avatar_name holds a control character - not used");
            return 0;
        }
        g_name[i] = c;
    }
    g_nameLen = len;
    return !(len == PATCH_NAME_LEN && memcmp(g_name, PATCH_NAME, sizeof PATCH_NAME) == 0);
}

static const uint8_t LENGTH_CALL_EXPECT[5] = { 0xE8, 0x48, 0xF8, 0xF2, 0xFF };
static const uint8_t READ_CALL_EXPECT[5]   = { 0xE8, 0x7A, 0xF8, 0xF2, 0xFF };

static const U9PatchSpec LENGTH_SPEC = {
    "AVATAR_NAME_ENTRY_LENGTH", U9PATCH_CALLSITE, U9SYM_SPOKENLINE_ENTRY_LENGTH_CALL,
    5, LENGTH_CALL_EXPECT, 5, U9PF_EXCLUSIVE
};
static const U9PatchSpec READ_SPEC = {
    "AVATAR_NAME_READ_ENTRY", U9PATCH_CALLSITE, U9SYM_SPOKENLINE_READ_ENTRY_CALL,
    5, READ_CALL_EXPECT, 5, U9PF_EXCLUSIVE
};

void u9avatarname_install(void)
{
    if (!load_name()) return;

    o_read = (ReadEntryFn)u9patch_install(&READ_SPEC, (void *)h_read);
    if (!o_read) { u9log_line("AVATAR NAME: the text read was refused (see the manifest) - lines keep Grandor"); return; }
    o_length = (EntryLengthFn)u9patch_install(&LENGTH_SPEC, (void *)h_length);
    if (!o_length) { u9log_line("AVATAR NAME: the text length was refused (see the manifest) - lines keep Grandor"); return; }
    g_on = 1;
    u9log_line("AVATAR NAME: the Dialogue Patch's \"Grandor\" reads as avatar_name (%u characters)", g_nameLen);
}

void u9avatarname_report(void)
{
    if (g_on)
        u9log_line("  AVATAR NAME: %u lines renamed, %u entries not readable", g_linesRenamed, g_failures);
}
