#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Build the native development host, then rebuild/publish only its C++ UI."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]



def sources():
    paths = [path for path in (ROOT / "src").rglob("*") if path.suffix in {".cpp", ".h", ".hpp", ".cc", ".cxx"}]
    paths += [ROOT / "CMakeLists.txt", ROOT / "CMakePresets.json"]
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).digest()
            for path in paths if path.is_file()}


def changed(before, after):
    return {path for path in before.keys() | after.keys() if before.get(path) != after.get(path)}


def requires_restart(edits):
    return {path for path in edits if not path.startswith("src/ui/")}


def publish(directory):
    marker = directory / "ui-ready"
    temporary = directory / "ui-ready.tmp"
    temporary.write_text(str(uuid.uuid4()), encoding="utf-8")
    os.replace(temporary, marker)


def build(target):
    return subprocess.run(["cmake", "--build", "--preset", "dev", "--target", target,
                           "--parallel", "4"], cwd=ROOT).returncode == 0


def main():
    subprocess.run(["cmake", "--preset", "dev"], cwd=ROOT, check=True)
    if not build("Opentone"):
        return 1
    configuration = ROOT / "build/dev/Opentone_artefacts/Debug"
    directory = configuration if sys.platform == "win32" else ROOT / "build/dev/live"
    executable = (configuration / "Opentone.app/Contents/MacOS/Opentone" if sys.platform == "darwin"
                  else configuration / ("Opentone.exe" if sys.platform == "win32" else "Opentone"))
    publish(directory)
    log = (ROOT / "build/dev/live-ui.log").open("a", encoding="utf-8")
    process_group = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP} if sys.platform == "win32"
                     else {"start_new_session": True})
    process = subprocess.Popen([str(executable)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, **process_group)
    print("Live C++ UI running. Edit src/ui/ components; save to rebuild and reload.", flush=True)
    print("Host log: build/dev/live-ui.log. Ctrl+C stops watching and leaves the app open.", flush=True)
    previous = sources()
    try:
        while process.poll() is None:
            time.sleep(0.25)
            current = sources()
            edits = changed(previous, current)
            if not edits:
                continue
            # Debounce normal editor saves and coalesce consecutive changes.
            time.sleep(0.3)
            current = sources()
            edits = changed(previous, current)
            if requires_restart(edits):
                print("Host/runtime files changed. Save and close the dev app, then rerun tools/dev.py.\n"
                      + "\n".join(sorted(requires_restart(edits))), flush=True)
                return 2
            previous = current
            print("Rebuilding UI: " + ", ".join(sorted(edits)), flush=True)
            if build("OpentoneUI"):
                restart = requires_restart(changed(previous, sources()))
                if restart:
                    print("Host/runtime files changed during the build. Save and close the dev app, then rerun tools/dev.py.\n"
                          + "\n".join(sorted(restart)), flush=True)
                    return 2
                publish(directory)
                print("UI published; the host will reload when editing/file operations are idle.", flush=True)
            else:
                print("Build failed; the current editor stays active. Save a correction to retry.", flush=True)
        return process.returncode
    except KeyboardInterrupt:
        print("Watcher stopped. Close the app normally to preserve unsaved-change handling.", flush=True)
        return 0
    finally:
        log.close()


if __name__ == "__main__":
    raise SystemExit(main())
