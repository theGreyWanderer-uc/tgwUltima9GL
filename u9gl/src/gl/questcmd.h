#ifndef U9GL_QUESTCMD_H
#define U9GL_QUESTCMD_H

void u9quest_datum_command(const char *arg, void (*out)(const char *fmt, ...));

void u9quest_gate_command(const char *arg, void (*out)(const char *fmt, ...));

void u9quest_show_command(const char *arg, void (*out)(const char *fmt, ...));

#endif
