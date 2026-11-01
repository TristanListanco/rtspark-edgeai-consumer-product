# Stamp the firmware with `git describe` (e.g. "m3-diagnostics-2-g1e84cf0-dirty") so every
# boot log and every captured dataset session records exactly which build produced it.
import subprocess

Import("env")

try:
    version = subprocess.check_output(
        ["git", "describe", "--always", "--dirty", "--tags"], cwd=env["PROJECT_DIR"]
    ).decode().strip()
except Exception:
    version = "unknown"

env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
print("Firmware version: %s" % version)
