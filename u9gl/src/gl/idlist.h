#ifndef U9GL_IDLIST_H
#define U9GL_IDLIST_H

static inline int u9_id_listed(const char *list, int id)
{
    char key[12], t[11];
    int n = 0, k = 0, i;
    unsigned v;
    const char *p;
    if (!list || !list[0]) return 0;
    v = id < 0 ? (unsigned)(-(long long)id) : (unsigned)id;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    if (id < 0) key[n++] = '-';
    while (k) key[n++] = t[--k];
    for (p = list; *p; p++) {
        if (*p != ',') continue;
        for (i = 0; i < n && p[1 + i] == key[i]; i++) {}
        if (i == n && p[1 + n] == ',') return 1;
    }
    return 0;
}

#endif
