"""Check that u9gl.ini, u9gl.diag.ini and the code agree about settings.

Run by u9gl/build.sh before compiling; exits 1 on any error. Checks:

  - every key the code reads (u9cfg_int / u9cfg_str with a literal key) is
    listed in u9gl.ini or u9gl.diag.ini - 44 were not, found 2026-10-03;
  - no key is listed in both files, and every listed key is read by the code;
  - every value in u9gl.diag.ini is the compiled default. That file is not
    shipped, so players get the compiled default; the file must say so;
  - the code never asks for a renamed key's OLD name. config.c stores the old
    name under the new one, so such a lookup would silently never match;
  - the launcher's rename table (U9glIni.RenamedKeys) matches config.c's;
  - no ini line is longer than config.c reads (255 characters).

Usage: python tools/check_ini_keys.py   (from anywhere)
"""

import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
U9GL = os.path.join(ROOT, "u9gl")
SRC = os.path.join(U9GL, "src")
MAIN = os.path.join(U9GL, "u9gl.ini")
DIAG = os.path.join(U9GL, "u9gl.diag.ini")
CONFIG_C = os.path.join(SRC, "injection", "config.c")
LAUNCHER_INI = os.path.join(ROOT, "launcher", "U9GL.Core", "Ini", "U9glIni.cs")
LINE_MAX = 255

READ = re.compile(r'u9cfg_(?:int|str)\(\s*"([A-Za-z0-9_]+)"\s*,\s*([^)]*?)\s*\)')
ENTRY = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*$")
RENAMED = re.compile(r'\{\s*"([a-z0-9_]+)"\s*,\s*"([a-z0-9_]+)"\s*\}')
LAUNCHER_RENAMED = re.compile(r'\["([a-z0-9_]+)"\]\s*=\s*"([a-z0-9_]+)"')


def code_reads():
    """key -> set of default expressions, and key -> first 'file:line'."""
    defaults = collections.defaultdict(set)
    where = {}
    for folder, _, files in os.walk(SRC):
        for name in files:
            if not name.endswith((".c", ".h")):
                continue
            path = os.path.join(folder, name)
            text = open(path, encoding="latin-1").read()
            for m in READ.finditer(text):
                key = m.group(1).lower()
                defaults[key].add(m.group(2))
                line = text.count("\n", 0, m.start()) + 1
                where.setdefault(key, "%s:%d" % (os.path.relpath(path, ROOT), line))
    return defaults, where


def ini_entries(path):
    """key -> (value, line number); errors for duplicates and long lines."""
    entries, errors = {}, []
    with open(path, encoding="latin-1") as f:
        for n, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if len(line) > LINE_MAX:
                errors.append("%s:%d is %d characters; config.c cuts lines at %d"
                              % (os.path.basename(path), n, len(line), LINE_MAX))
            s = line.strip()
            if not s or s[0] in "#;":
                continue
            m = ENTRY.match(s)
            if not m:
                continue
            key = m.group(1).lower()
            if key in entries:
                errors.append("%s:%d lists '%s' again (first at line %d)"
                              % (os.path.basename(path), n, key, entries[key][1]))
            entries[key] = (m.group(2), n)
    return entries, errors


def as_value(text):
    """A comparable form of an ini value or a C default: int where possible."""
    t = text.strip()
    if len(t) >= 2 and t[0] == '"' and t[-1] == '"':
        return t[1:-1]
    low = t.lower()
    if low in ("on", "true", "yes"):
        return 1
    if low in ("off", "false", "no"):
        return 0
    if re.fullmatch(r"[0-9A-Fa-fx+\-*/ ()]+", t):
        try:
            return int(eval(t.replace("/", "//"), {"__builtins__": {}}))
        except Exception:
            pass
    return t


def main():
    errors, notes = [], []
    defaults, where = code_reads()
    main_keys, e1 = ini_entries(MAIN)
    diag_keys, e2 = ini_entries(DIAG)
    errors += e1 + e2

    renamed = RENAMED.findall(open(CONFIG_C, encoding="latin-1").read())
    old_names = {old: new for old, new in renamed}

    if os.path.exists(LAUNCHER_INI):
        launcher = dict(LAUNCHER_RENAMED.findall(open(LAUNCHER_INI, encoding="utf-8").read()))
        if launcher != old_names:
            for old in sorted(set(launcher) | set(old_names)):
                if launcher.get(old) != old_names.get(old):
                    errors.append("renamed key '%s': config.c says %s, U9glIni.RenamedKeys says %s"
                                  % (old, old_names.get(old), launcher.get(old)))

    for key in sorted(defaults):
        if key in old_names:
            errors.append("%s reads '%s', a renamed key - ask for '%s'"
                          % (where[key], key, old_names[key]))
        elif key not in main_keys and key not in diag_keys:
            errors.append("%s reads '%s', which neither u9gl.ini nor u9gl.diag.ini lists"
                          % (where[key], key))
        values = {as_value(d) for d in defaults[key]}
        if len(values) > 1:
            notes.append("'%s' has different defaults at different reads: %s"
                         % (key, ", ".join(sorted(defaults[key]))))

    for key in sorted(set(main_keys) & set(diag_keys)):
        errors.append("'%s' is listed in both u9gl.ini and u9gl.diag.ini" % key)

    for name, entries in (("u9gl.ini", main_keys), ("u9gl.diag.ini", diag_keys)):
        for key, (_, n) in sorted(entries.items()):
            if key in old_names:
                errors.append("%s:%d lists the old name '%s' - it is '%s' now"
                              % (name, n, key, old_names[key]))
            elif key not in defaults:
                errors.append("%s:%d lists '%s', which the code never reads" % (name, n, key))

    for key, (value, n) in sorted(diag_keys.items()):
        if key not in defaults:
            continue
        compiled = {as_value(d) for d in defaults[key]}
        if as_value(value) not in compiled:
            if all(isinstance(c, str) and re.fullmatch(r"[A-Z_][A-Z0-9_]*", c) for c in compiled):
                notes.append("u9gl.diag.ini:%d '%s' = %s; the default is a macro (%s), not checked"
                             % (n, key, value, ", ".join(sorted(defaults[key]))))
                continue
            errors.append("u9gl.diag.ini:%d '%s' = %s, but the compiled default is %s - "
                          "players get the default, so the file must say it"
                          % (n, key, value, ", ".join(sorted(defaults[key]))))

    for note in notes:
        print("ini keys: note: " + note)
    if errors:
        for error in errors:
            print("ini keys: ERROR " + error, file=sys.stderr)
        print("ini keys: %d error(s); list each key the code reads in u9gl.ini (a player's setting) "
              "or u9gl.diag.ini (a research switch), at the code's default" % len(errors), file=sys.stderr)
        return 1
    print("ini keys: %d read by the code, %d in u9gl.ini, %d in u9gl.diag.ini - consistent"
          % (len(defaults), len(main_keys), len(diag_keys)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
