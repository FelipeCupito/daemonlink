#!/usr/bin/env python3
"""
DaemonLink — apply_patches.py

Dual-mode helper:

  * Standalone CLI:  `python scripts/apply_patches.py [--reverse] [--status]`
  * PlatformIO pre-script:  declared as `extra_scripts = pre:scripts/apply_patches.py`
    in platformio.ini. Runs once before each build to ensure the Marauder
    submodule is initialized and patched.

Behavior:
  1. Ensures `external/ESP32Marauder` is initialized (runs `git submodule
     update --init --recursive` if the working tree is empty).
  2. Detects whether `patches/marauder.patch` is already applied (via
     `git apply --reverse --check`) and skips reapplication. Idempotent
     across builds — safe to run on every `pio run`.
  3. Aborts the build with a clear error message if the patch cannot
     apply (typical cause: upstream Marauder moved and the patch needs
     re-rolling).

Exit codes (CLI mode):
  0 = patched (or already patched / nothing to do)
  1 = error (submodule missing, patch conflict, etc.)
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------
ROOT       = Path(__file__).resolve().parents[1]
SUBMODULE  = ROOT / "external" / "ESP32Marauder"
SKETCH_DIR = SUBMODULE / "esp32_marauder"
PATCH_FILE = ROOT / "patches" / "marauder.patch"

# ANSI colors for console (skipped automatically when not a TTY).
_USE_COLOR = sys.stdout.isatty()
def _c(code: str, s: str) -> str:
    return f"\033[{code}m{s}\033[0m" if _USE_COLOR else s
def info(msg: str)  -> None: print(_c("36", "[apply_patches] ") + msg)
def ok(msg: str)    -> None: print(_c("32", "[apply_patches] ") + msg)
def warn(msg: str)  -> None: print(_c("33", "[apply_patches] ") + msg)
def err(msg: str)   -> None: print(_c("31", "[apply_patches] ") + msg, file=sys.stderr)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _git_available() -> bool:
    return shutil.which("git") is not None


def _run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess:
    """Run a subprocess capturing stdout/stderr without raising."""
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def ensure_submodule() -> bool:
    """Initialize the submodule if its working tree is empty.
    Returns True if Marauder source is available after this call."""
    if SKETCH_DIR.exists() and any(SKETCH_DIR.iterdir()):
        return True

    if not _git_available():
        err("git not found in PATH; cannot initialize submodule.")
        return False

    info("Submodule appears empty; running `git submodule update --init --recursive`...")
    r = _run(["git", "submodule", "update", "--init", "--recursive"], cwd=ROOT)
    if r.returncode != 0:
        err("Failed to initialize submodule:")
        err(r.stderr.strip())
        return False

    if not SKETCH_DIR.exists():
        err(f"Submodule path missing after init: {SKETCH_DIR}")
        return False
    return True


def patch_state() -> str:
    """Return one of: 'applied', 'pending', 'conflict', 'missing-patch'."""
    if not PATCH_FILE.exists():
        return "missing-patch"

    # `git apply --reverse --check` succeeds <=> patch is already in tree.
    r = _run(["git", "apply", "--reverse", "--check", str(PATCH_FILE)], cwd=SUBMODULE)
    if r.returncode == 0:
        return "applied"

    # Otherwise, can it be applied forward?
    r = _run(["git", "apply", "--check", str(PATCH_FILE)], cwd=SUBMODULE)
    if r.returncode == 0:
        return "pending"

    return "conflict"


def apply_forward() -> int:
    state = patch_state()
    if state == "missing-patch":
        warn(f"No patch file at {PATCH_FILE.relative_to(ROOT)} — nothing to apply.")
        return 0
    if state == "applied":
        ok("Patch already applied — skipping.")
        return 0
    if state == "conflict":
        err("Patch does NOT apply cleanly. Upstream Marauder probably moved.")
        err(f"  Inspect: git -C {SUBMODULE.relative_to(ROOT)} apply --check {PATCH_FILE.relative_to(ROOT)}")
        return 1

    info(f"Applying {PATCH_FILE.relative_to(ROOT)} to submodule...")
    r = _run(["git", "apply", str(PATCH_FILE)], cwd=SUBMODULE)
    if r.returncode != 0:
        err("git apply failed unexpectedly:")
        err(r.stderr.strip())
        return 1
    ok("Patch applied.")
    return 0


def apply_reverse() -> int:
    if not PATCH_FILE.exists():
        warn("No patch file — nothing to revert.")
        return 0
    state = patch_state()
    if state == "pending":
        ok("Patch is not currently applied — nothing to revert.")
        return 0
    if state == "applied":
        info("Reverting patch...")
        r = _run(["git", "apply", "--reverse", str(PATCH_FILE)], cwd=SUBMODULE)
        if r.returncode != 0:
            err("Reverse apply failed:")
            err(r.stderr.strip())
            return 1
        ok("Patch reverted.")
        return 0
    err("Submodule state is dirty/conflicting; cannot safely revert.")
    return 1


def show_status() -> int:
    info(f"ROOT       = {ROOT}")
    info(f"SUBMODULE  = {SUBMODULE}")
    info(f"PATCH_FILE = {PATCH_FILE}")
    if not SKETCH_DIR.exists():
        warn("Submodule not initialized.")
        return 0
    state = patch_state()
    info(f"State: {state}")
    return 0


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------
def main_cli() -> int:
    p = argparse.ArgumentParser(description="Apply / revert the DaemonLink patch on the Marauder submodule.")
    g = p.add_mutually_exclusive_group()
    g.add_argument("--reverse", action="store_true", help="Revert the patch instead of applying.")
    g.add_argument("--status",  action="store_true", help="Print current patch state and exit.")
    args = p.parse_args()

    if not ensure_submodule():
        return 1
    if args.status:
        return show_status()
    if args.reverse:
        return apply_reverse()
    return apply_forward()


def _platformio_entry() -> None:
    """Called when this file is sourced by PlatformIO as an extra_script."""
    info("PlatformIO pre-build hook running.")
    if not ensure_submodule():
        sys.exit(1)
    rc = apply_forward()
    if rc != 0:
        sys.exit(rc)


# Detect whether we are inside PlatformIO (which provides Import("env")).
try:
    Import("env")  # type: ignore[name-defined]  # noqa: F821
    _platformio_entry()
except NameError:
    if __name__ == "__main__":
        sys.exit(main_cli())
