Import("env")
import os
import re

# CHANGELOG.md ist die einzige Quelle der Wahrheit fuer die Firmware-Version:
# die oberste Release-Ueberschrift "## [x.y.z] - Datum" (also nicht
# [Unreleased]) wird als FW_VERSION an den Compiler uebergeben.
changelog = os.path.join(env["PROJECT_DIR"], "CHANGELOG.md")
with open(changelog, encoding="utf-8") as f:
    m = re.search(r"^##\s+\[(\d+\.\d+\.\d+)\]", f.read(), re.MULTILINE)
if not m:
    raise SystemExit("CHANGELOG.md: keine Release-Ueberschrift '## [x.y.z]' gefunden")

version = m.group(1)
print("FW_VERSION = %s (aus CHANGELOG.md)" % version)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
