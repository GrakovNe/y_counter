# PlatformIO pre-build script: gzips the web pages into C headers in the
# build directory and defines APP_VER from the git revision.
import gzip
import os
import subprocess

Import("env")  # noqa: F821

proj = env.subst("$PROJECT_DIR")
gen = os.path.join(env.subst("$BUILD_DIR"), "gen_web")
os.makedirs(gen, exist_ok=True)

for page in ("setup", "main", "info"):
    src = os.path.join(proj, "web", page + ".html")
    out = os.path.join(gen, page + "_html.h")
    newest_src = max(os.path.getmtime(src), os.path.getmtime(os.path.join(proj, "tools", "prebuild.py")))
    if os.path.exists(out) and os.path.getmtime(out) >= newest_src:
        continue
    with open(src, "rb") as f:
        data = f.read()
    gz = gzip.compress(data, compresslevel=9, mtime=0)
    sym = page + "_html_gz"
    lines = [
        "// Generated from web/%s.html by tools/prebuild.py. Do not edit." % page,
        "#pragma once",
        "#include <pgmspace.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "static const uint8_t %s[] PROGMEM = {" % sym,
    ]
    for i in range(0, len(gz), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in gz[i:i + 16]) + ",")
    lines += ["};", "static const size_t %s_len = %d;" % (sym, len(gz)), ""]
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print("gen_web: %s.html -> %d bytes gzip" % (page, len(gz)))

env.Append(CPPPATH=[gen])

try:
    ver = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=proj,
                                  stderr=subprocess.DEVNULL).decode().strip()
    dirty = subprocess.check_output(["git", "status", "--porcelain"], cwd=proj,
                                    stderr=subprocess.DEVNULL).strip() != b""
    if dirty:
        ver += "-dirty"
except Exception:
    ver = "dev"
env.Append(CPPDEFINES=[("APP_VER", env.StringifyMacro(ver))])
