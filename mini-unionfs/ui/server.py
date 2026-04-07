#!/usr/bin/env python3
"""
Mini-UnionFS Dashboard Server
Member 1 (UI): Flask backend — REST API + serves the HTML dashboard.
"""

import os
import subprocess
import shutil
import signal
import time
import json
from flask import Flask, jsonify, request, send_from_directory

app = Flask(__name__, static_folder="static")

# ── Paths (relative to the project root, one level up from ui/) ──────
BASE_DIR    = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LOWER_DIR   = os.path.join(BASE_DIR, "lower")
UPPER_DIR   = os.path.join(BASE_DIR, "upper")
MNT_DIR     = os.path.join(BASE_DIR, "mnt")
BINARY      = os.path.join(BASE_DIR, "mini_unionfs")

fuse_process = None   # Track the background FUSE process

# ─────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────

def is_mounted():
    """Return True if mnt/ is currently a FUSE mount point."""
    try:
        result = subprocess.run(["mountpoint", "-q", MNT_DIR])
        return result.returncode == 0
    except Exception:
        return False


def ensure_dirs():
    os.makedirs(LOWER_DIR, exist_ok=True)
    os.makedirs(UPPER_DIR, exist_ok=True)
    os.makedirs(MNT_DIR,   exist_ok=True)


def list_layer(path):
    """Return a list of {name, type, size, layer} dicts for a directory."""
    entries = []
    if not os.path.isdir(path):
        return entries
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        stat = os.stat(full)
        entries.append({
            "name": name,
            "type": "dir" if os.path.isdir(full) else "file",
            "size": stat.st_size,
            "modified": int(stat.st_mtime),
            "is_whiteout": name.startswith(".wh."),
        })
    return entries


def read_file_safe(path, max_bytes=4096):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read(max_bytes)
    except Exception as e:
        return f"[error reading file: {e}]"


# ─────────────────────────────────────────────────────────────────────
# Static dashboard
# ─────────────────────────────────────────────────────────────────────

@app.route("/")
def index():
    return send_from_directory("static", "index.html")


# ─────────────────────────────────────────────────────────────────────
# API — Mount / Unmount
# ─────────────────────────────────────────────────────────────────────

@app.route("/api/status")
def status():
    mounted = is_mounted()
    return jsonify({
        "mounted": mounted,
        "lower_dir": LOWER_DIR,
        "upper_dir": UPPER_DIR,
        "mnt_dir":   MNT_DIR,
        "binary_exists": os.path.isfile(BINARY),
    })


@app.route("/api/mount", methods=["POST"])
def mount():
    global fuse_process
    if is_mounted():
        return jsonify({"ok": False, "msg": "Already mounted."})
    if not os.path.isfile(BINARY):
        return jsonify({"ok": False, "msg": f"Binary not found: {BINARY}. Run 'make' first."})
    ensure_dirs()
    try:
        fuse_process = subprocess.Popen(
            [BINARY, LOWER_DIR, UPPER_DIR, MNT_DIR],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        time.sleep(1.0)
        if is_mounted():
            return jsonify({"ok": True, "msg": "Filesystem mounted successfully."})
        else:
            return jsonify({"ok": False, "msg": "Mount command ran but mountpoint not active. Check binary."})
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


@app.route("/api/unmount", methods=["POST"])
def unmount():
    global fuse_process
    if not is_mounted():
        return jsonify({"ok": False, "msg": "Not mounted."})
    try:
        subprocess.run(["fusermount3", "-u", MNT_DIR], check=True)
        if fuse_process:
            fuse_process.wait(timeout=3)
            fuse_process = None
        return jsonify({"ok": True, "msg": "Filesystem unmounted."})
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


# ─────────────────────────────────────────────────────────────────────
# API — Layer inspection
# ─────────────────────────────────────────────────────────────────────

@app.route("/api/layers")
def layers():
    return jsonify({
        "lower": list_layer(LOWER_DIR),
        "upper": list_layer(UPPER_DIR),
        "mount": list_layer(MNT_DIR) if is_mounted() else [],
    })


@app.route("/api/read")
def read_file():
    """Read a file from a given layer. ?layer=lower|upper|mount&name=filename"""
    layer = request.args.get("layer", "mount")
    name  = request.args.get("name", "")
    if not name or ".." in name:
        return jsonify({"ok": False, "content": "Invalid filename."})
    layer_map = {"lower": LOWER_DIR, "upper": UPPER_DIR, "mount": MNT_DIR}
    base = layer_map.get(layer)
    if not base:
        return jsonify({"ok": False, "content": "Unknown layer."})
    path = os.path.join(base, name)
    if not os.path.isfile(path):
        return jsonify({"ok": False, "content": f"File not found: {path}"})
    content = read_file_safe(path)
    return jsonify({"ok": True, "content": content, "path": path})


# ─────────────────────────────────────────────────────────────────────
# API — Demo operations (for the live demo panel)
# ─────────────────────────────────────────────────────────────────────

@app.route("/api/demo/seed", methods=["POST"])
def demo_seed():
    """Populate the lower layer with sample files for the demo."""
    ensure_dirs()
    try:
        with open(os.path.join(LOWER_DIR, "base.txt"), "w") as f:
            f.write("base_only_content\n")
        with open(os.path.join(LOWER_DIR, "delete_me.txt"), "w") as f:
            f.write("to_be_deleted\n")
        with open(os.path.join(LOWER_DIR, "config.txt"), "w") as f:
            f.write("[database]\nhost=localhost\nport=5432\n")
        return jsonify({"ok": True, "msg": "Lower layer seeded with base.txt, delete_me.txt, config.txt"})
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


@app.route("/api/demo/cow", methods=["POST"])
def demo_cow():
    """Trigger CoW: append text to base.txt via the mount."""
    if not is_mounted():
        return jsonify({"ok": False, "msg": "Mount the filesystem first."})
    try:
        mnt_file = os.path.join(MNT_DIR, "base.txt")
        with open(mnt_file, "a") as f:
            f.write("modified_via_cow\n")
        time.sleep(0.2)
        in_upper = os.path.isfile(os.path.join(UPPER_DIR, "base.txt"))
        lower_clean = "modified_via_cow" not in read_file_safe(os.path.join(LOWER_DIR, "base.txt"))
        return jsonify({
            "ok": True,
            "msg": "CoW triggered! File copied to upper layer and modified.",
            "in_upper": in_upper,
            "lower_untouched": lower_clean,
        })
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


@app.route("/api/demo/whiteout", methods=["POST"])
def demo_whiteout():
    """Delete delete_me.txt via the mount to trigger whiteout."""
    if not is_mounted():
        return jsonify({"ok": False, "msg": "Mount the filesystem first."})
    mnt_file   = os.path.join(MNT_DIR,   "delete_me.txt")
    lower_file = os.path.join(LOWER_DIR, "delete_me.txt")
    wh_file    = os.path.join(UPPER_DIR, ".wh.delete_me.txt")
    if not os.path.isfile(mnt_file):
        return jsonify({"ok": False, "msg": "delete_me.txt not found in mount. Re-seed first."})
    try:
        os.remove(mnt_file)
        time.sleep(0.2)
        return jsonify({
            "ok": True,
            "msg": "Whiteout created! File hidden from mount, original preserved in lower.",
            "whiteout_exists":  os.path.isfile(wh_file),
            "lower_preserved":  os.path.isfile(lower_file),
            "hidden_from_mount": not os.path.isfile(mnt_file),
        })
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


@app.route("/api/demo/create", methods=["POST"])
def demo_create():
    """Create a new file via the mount."""
    if not is_mounted():
        return jsonify({"ok": False, "msg": "Mount the filesystem first."})
    data = request.get_json(silent=True) or {}
    name    = data.get("name", "newfile.txt")
    content = data.get("content", "Hello from the UI!\n")
    if ".." in name or "/" in name:
        return jsonify({"ok": False, "msg": "Invalid filename."})
    try:
        with open(os.path.join(MNT_DIR, name), "w") as f:
            f.write(content)
        time.sleep(0.2)
        in_upper = os.path.isfile(os.path.join(UPPER_DIR, name))
        return jsonify({"ok": True, "msg": f"'{name}' created in mount → stored in upper layer.", "in_upper": in_upper})
    except Exception as e:
        return jsonify({"ok": False, "msg": str(e)})


@app.route("/api/demo/reset", methods=["POST"])
def demo_reset():
    """Unmount, wipe layers, start fresh."""
    global fuse_process
    if is_mounted():
        subprocess.run(["fusermount3", "-u", MNT_DIR], capture_output=True)
        if fuse_process:
            try: fuse_process.wait(timeout=2)
            except: pass
            fuse_process = None
        time.sleep(0.5)
    # Wipe and recreate
    for d in [LOWER_DIR, UPPER_DIR]:
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
    os.makedirs(MNT_DIR, exist_ok=True)
    return jsonify({"ok": True, "msg": "Layers wiped. Ready for a fresh demo."})


# ─────────────────────────────────────────────────────────────────────
# Run
# ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    ensure_dirs()
    print("=" * 50)
    print("  Mini-UnionFS Dashboard")
    print("  Open http://localhost:5000 in your browser")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
