#!/bin/bash
# ─────────────────────────────────────────────────────────────────────
# Mini-UnionFS UI Launcher
# Run from the project root: bash ui/start_ui.sh
# ─────────────────────────────────────────────────────────────────────
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "========================================"
echo "  Mini-UnionFS Dashboard Launcher"
echo "========================================"

# Build the FUSE binary if not present
if [ ! -f "$PROJECT_DIR/mini_unionfs" ]; then
  echo "[*] Binary not found — building..."
  (cd "$PROJECT_DIR" && make)
  echo "[✓] Build complete."
else
  echo "[✓] Binary found: $PROJECT_DIR/mini_unionfs"
fi

# Create layer directories
mkdir -p "$PROJECT_DIR/lower" "$PROJECT_DIR/upper" "$PROJECT_DIR/mnt"

# Install Python deps if needed
echo "[*] Checking Python dependencies..."
pip3 install -q flask 2>/dev/null || pip install -q flask 2>/dev/null

echo ""
echo "  ┌──────────────────────────────────────┐"
echo "  │  Dashboard → http://localhost:5000    │"
echo "  │  Press Ctrl+C to stop                │"
echo "  └──────────────────────────────────────┘"
echo ""

cd "$SCRIPT_DIR"
python3 server.py
