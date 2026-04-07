#!/bin/bash
# Mini-UnionFS Automated Test Suite
# Place this script in the same directory as your compiled FUSE binary.

FUSE_BINARY="./mini_unionfs"
TEST_DIR="./unionfs_test_env"
LOWER_DIR="$TEST_DIR/lower"
UPPER_DIR="$TEST_DIR/upper"
MOUNT_DIR="$TEST_DIR/mnt"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0

pass() { echo -e "${GREEN}PASSED${NC}"; ((PASS++)); }
fail() { echo -e "${RED}FAILED${NC}"; ((FAIL++)); }

echo "======================================"
echo "  Mini-UnionFS Test Suite"
echo "======================================"

# ── Setup ──────────────────────────────────────────────────────────────
# Clean any previous run
fusermount3 -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null || true
rm -rf "$TEST_DIR"
mkdir -p "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR"

# Populate lower layer
echo "base_only_content"  > "$LOWER_DIR/base.txt"
echo "to_be_deleted"      > "$LOWER_DIR/delete_me.txt"
echo "shared_lower"       > "$LOWER_DIR/shared.txt"
echo "shared_upper"       > "$UPPER_DIR/shared.txt"

# Mount (run in background, -f foreground flag is NOT used here)
"$FUSE_BINARY" "$LOWER_DIR" "$UPPER_DIR" "$MOUNT_DIR" &
FUSE_PID=$!
sleep 1   # Give FUSE a moment to mount

# Check mount succeeded
if ! mountpoint -q "$MOUNT_DIR"; then
    echo -e "${RED}ERROR: FUSE did not mount. Aborting tests.${NC}"
    kill "$FUSE_PID" 2>/dev/null
    exit 1
fi

# ── Test 1: Layer Visibility ───────────────────────────────────────────
echo -n "Test 1: Layer Visibility (lower file visible)... "
if grep -q "base_only_content" "$MOUNT_DIR/base.txt" 2>/dev/null; then
    pass
else
    fail
fi

# ── Test 2: Upper Takes Precedence ────────────────────────────────────
echo -n "Test 2: Upper layer takes precedence over lower... "
if grep -q "shared_upper" "$MOUNT_DIR/shared.txt" 2>/dev/null; then
    pass
else
    fail
fi

# ── Test 3: Copy-on-Write ─────────────────────────────────────────────
echo -n "Test 3: Copy-on-Write (modify lower file)... "
echo "modified_content" >> "$MOUNT_DIR/base.txt" 2>/dev/null
sleep 0.2
COW_MOUNT=$(grep -c "modified_content" "$MOUNT_DIR/base.txt" 2>/dev/null)
COW_UPPER=$(grep -c "modified_content" "$UPPER_DIR/base.txt" 2>/dev/null)
COW_LOWER=$(grep -c "modified_content" "$LOWER_DIR/base.txt" 2>/dev/null)
if [ "$COW_MOUNT" -eq 1 ] && [ "$COW_UPPER" -eq 1 ] && [ "$COW_LOWER" -eq 0 ]; then
    pass
else
    fail
    echo "  mount=$COW_MOUNT  upper=$COW_UPPER  lower=$COW_LOWER"
fi

# ── Test 4: Whiteout (delete lower file) ──────────────────────────────
echo -n "Test 4: Whiteout mechanism (delete lower-only file)... "
rm "$MOUNT_DIR/delete_me.txt" 2>/dev/null
sleep 0.2
if [ ! -f "$MOUNT_DIR/delete_me.txt" ] && \
   [   -f "$LOWER_DIR/delete_me.txt" ] && \
   [   -f "$UPPER_DIR/.wh.delete_me.txt" ]; then
    pass
else
    fail
    echo "  mount_exists=$([ -f "$MOUNT_DIR/delete_me.txt" ] && echo yes || echo no)"
    echo "  lower_exists=$([ -f "$LOWER_DIR/delete_me.txt" ] && echo yes || echo no)"
    echo "  whiteout_exists=$([ -f "$UPPER_DIR/.wh.delete_me.txt" ] && echo yes || echo no)"
fi

# ── Test 5: Create new file ────────────────────────────────────────────
echo -n "Test 5: Create new file in mount (goes to upper)... "
echo "new_content" > "$MOUNT_DIR/newfile.txt" 2>/dev/null
sleep 0.2
if [ -f "$UPPER_DIR/newfile.txt" ] && grep -q "new_content" "$MOUNT_DIR/newfile.txt" 2>/dev/null; then
    pass
else
    fail
fi

# ── Test 6: mkdir ─────────────────────────────────────────────────────
echo -n "Test 6: mkdir creates directory in upper... "
mkdir "$MOUNT_DIR/newdir" 2>/dev/null
sleep 0.2
if [ -d "$UPPER_DIR/newdir" ] && [ -d "$MOUNT_DIR/newdir" ]; then
    pass
else
    fail
fi

# ── Test 7: Lower directory is untouched after CoW ────────────────────
echo -n "Test 7: Lower base.txt still has only original content... "
if grep -q "base_only_content" "$LOWER_DIR/base.txt" && \
   ! grep -q "modified_content" "$LOWER_DIR/base.txt"; then
    pass
else
    fail
fi

# ── Teardown ───────────────────────────────────────────────────────────
echo ""
echo "Unmounting..."
fusermount3 -u "$MOUNT_DIR" 2>/dev/null || umount "$MOUNT_DIR" 2>/dev/null
wait "$FUSE_PID" 2>/dev/null

rm -rf "$TEST_DIR"

echo "======================================"
echo -e "Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}"
echo "======================================"
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
