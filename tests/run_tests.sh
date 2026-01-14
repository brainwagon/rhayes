#!/bin/bash
# RIB Test Runner

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RENDER="$PROJECT_DIR/bin/render"
CATRIB="$PROJECT_DIR/bin/catrib"
RIB_DIR="$SCRIPT_DIR/rib"
REF_DIR="$SCRIPT_DIR/reference"
OUT_DIR="$SCRIPT_DIR/output"

PASS=0
FAIL=0
SKIP=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

run_parse_test() {
    local rib_file="$1"
    local name="$(basename "$rib_file" .rib)"

    if "$CATRIB" "$rib_file" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [parse] $name"
        ((PASS++))
    else
        echo -e "${RED}FAIL${NC} [parse] $name"
        ((FAIL++))
    fi
}

run_render_test() {
    local rib_file="$1"
    local rel_path="${rib_file#$RIB_DIR/}"
    local name="$(basename "$rib_file" .rib)"
    local ref_file="$REF_DIR/${rel_path%.rib}.png"
    local out_file="$OUT_DIR/${rel_path%.rib}.png"

    mkdir -p "$(dirname "$out_file")"

    # Create temp RIB with output redirected
    local temp_rib="$OUT_DIR/temp_$name.rib"
    sed "s|Display.*|Display \"$out_file\" \"file\" \"rgba\"|" "$rib_file" > "$temp_rib"

    if ! "$RENDER" "$temp_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [render] $name (render failed)"
        ((FAIL++))
        rm -f "$temp_rib"
        return
    fi
    rm -f "$temp_rib"

    if [ ! -f "$ref_file" ]; then
        echo -e "${YELLOW}SKIP${NC} [render] $name (no reference)"
        ((SKIP++))
        return
    fi

    if diff -q "$out_file" "$ref_file" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC} [render] $name"
        ((PASS++))
    else
        echo -e "${RED}FAIL${NC} [render] $name (output differs)"
        ((FAIL++))
    fi
}

run_roundtrip_test() {
    local rib_file="$1"
    local name="$(basename "$rib_file" .rib)"
    local out_rib="$OUT_DIR/roundtrip_$name.rib"

    if ! "$CATRIB" "$rib_file" -o "$out_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [roundtrip] $name (catrib failed)"
        ((FAIL++))
        return
    fi

    # Parse the output again
    if "$CATRIB" "$out_rib" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [roundtrip] $name"
        ((PASS++))
    else
        echo -e "${RED}FAIL${NC} [roundtrip] $name (re-parse failed)"
        ((FAIL++))
    fi
}

# Main
echo "=== RIB Test Suite ==="
echo ""

# Check for test RIB files
if [ ! -d "$RIB_DIR" ] || [ -z "$(find "$RIB_DIR" -name "*.rib" 2>/dev/null)" ]; then
    echo "No test RIB files found in $RIB_DIR"
    exit 1
fi

# Find all RIB files
for rib in $(find "$RIB_DIR" -name "*.rib" | sort); do
    run_parse_test "$rib"
    run_render_test "$rib"
    run_roundtrip_test "$rib"
done

echo ""
echo "=== Results ==="
echo -e "Passed: ${GREEN}$PASS${NC}"
echo -e "Failed: ${RED}$FAIL${NC}"
echo -e "Skipped: ${YELLOW}$SKIP${NC}"

exit $FAIL
