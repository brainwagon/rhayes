#!/bin/bash
# RIB Test Runner

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RENDER="$PROJECT_DIR/bin/render"
CATRIB="$PROJECT_DIR/bin/catrib"
SCENE2RIB="$PROJECT_DIR/bin/scene2rib"
RIB_DIR="$SCRIPT_DIR/rib"
REF_DIR="$SCRIPT_DIR/reference"
OUT_DIR="$SCRIPT_DIR/output"

PASS=0
FAIL=0
SKIP=0
ROW_COUNT=0

declare -a ROW_NAMES=()
declare -a ROW_TYPES=()
declare -a ROW_STATUS=()
declare -a ROW_REF_REL=()
declare -a ROW_OUT_REL=()
declare -a ROW_REF_MTIME=()
declare -a ROW_OUT_MTIME=()

RUN_TIMESTAMP="$(date '+%Y-%m-%d %H:%M:%S')"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

get_file_mtime() {
    local f="$1"
    if [ -f "$f" ]; then
        stat -c '%y' "$f" 2>/dev/null | cut -d'.' -f1
    else
        echo ""
    fi
}

record_test() {
    local name="$1" type="$2" status="$3"
    local ref_abs="${4:-}" out_abs="${5:-}"

    local ref_rel="" out_rel="" ref_mtime="" out_mtime=""

    if [ -n "$ref_abs" ] && [ -f "$ref_abs" ]; then
        ref_rel="${ref_abs#$REF_DIR/}"
        ref_mtime="$(get_file_mtime "$ref_abs")"
    fi
    if [ -n "$out_abs" ] && [ -f "$out_abs" ]; then
        out_rel="${out_abs#$OUT_DIR/}"
        out_mtime="$(get_file_mtime "$out_abs")"
    fi

    ROW_NAMES+=("$name")
    ROW_TYPES+=("$type")
    ROW_STATUS+=("$status")
    ROW_REF_REL+=("$ref_rel")
    ROW_OUT_REL+=("$out_rel")
    ROW_REF_MTIME+=("$ref_mtime")
    ROW_OUT_MTIME+=("$out_mtime")
    ((ROW_COUNT++)) || true
}

generate_html_report() {
    local report="$OUT_DIR/report.html"

    {
        cat <<'HTMLHEAD'
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>Test Report</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 24px; background: #f5f5f5; }
    h1 { color: #333; margin-bottom: 6px; }
    .meta { color: #555; margin-bottom: 8px; }
    .summary { margin-bottom: 20px; font-size: 1.05em; }
    .pass-count { color: #155724; font-weight: bold; }
    .fail-count { color: #721c24; font-weight: bold; }
    .skip-count { color: #856404; font-weight: bold; }
    table { border-collapse: collapse; width: 100%; background: white;
            box-shadow: 0 1px 3px rgba(0,0,0,0.15); }
    th, td { border: 1px solid #ddd; padding: 8px 12px; vertical-align: middle; }
    th { background: #e8e8e8; font-weight: bold; text-align: center; }
    tr:nth-child(even) td { background: #fafafa; }
    .col-name { text-align: left; font-family: monospace; white-space: nowrap; }
    .col-type { text-align: center; white-space: nowrap; color: #555; }
    .col-pass { background: #d4edda !important; color: #155724; font-weight: bold; text-align: center; }
    .col-fail { background: #f8d7da !important; color: #721c24; font-weight: bold; text-align: center; }
    .col-skip { background: #fff3cd !important; color: #856404; font-weight: bold; text-align: center; }
    .col-img { text-align: center; width: 220px; }
    figure { margin: 0; }
    img { max-width: 200px; max-height: 160px; display: block; margin: 0 auto;
          border: 1px solid #ccc; }
    figcaption { font-size: 0.72em; color: #666; margin-top: 4px; }
    .no-img { color: #bbb; font-style: italic; }
  </style>
</head>
<body>
HTMLHEAD

        echo "  <h1>Rhayes Test Report</h1>"
        echo "  <p class=\"meta\">Run at: <strong>${RUN_TIMESTAMP}</strong></p>"
        echo "  <div class=\"summary\">"
        echo "    &#x2714;&nbsp;<span class=\"pass-count\">Passed: ${PASS}</span> &nbsp;&nbsp;"
        echo "    &#x2718;&nbsp;<span class=\"fail-count\">Failed: ${FAIL}</span> &nbsp;&nbsp;"
        echo "    &#x25CB;&nbsp;<span class=\"skip-count\">Skipped: ${SKIP}</span>"
        echo "  </div>"

        cat <<'HTMLTABLE'
  <table>
    <thead>
      <tr>
        <th>Test Name</th>
        <th>Type</th>
        <th>Status</th>
        <th>Reference Image</th>
        <th>Generated Image</th>
      </tr>
    </thead>
    <tbody>
HTMLTABLE

        local i
        for ((i = 0; i < ROW_COUNT; i++)); do
            local name="${ROW_NAMES[$i]}"
            local type="${ROW_TYPES[$i]}"
            local status="${ROW_STATUS[$i]}"
            local ref_rel="${ROW_REF_REL[$i]}"
            local out_rel="${ROW_OUT_REL[$i]}"
            local ref_mtime="${ROW_REF_MTIME[$i]}"
            local out_mtime="${ROW_OUT_MTIME[$i]}"

            local sc
            case "$status" in
                PASS) sc="col-pass" ;;
                FAIL) sc="col-fail" ;;
                SKIP) sc="col-skip" ;;
                *)    sc="" ;;
            esac

            local ref_html out_html
            if [ -n "$ref_rel" ]; then
                ref_html="<figure><a href=\"../reference/${ref_rel}\" target=\"_blank\"><img src=\"../reference/${ref_rel}\" alt=\"reference\"></a><figcaption>${ref_mtime}</figcaption></figure>"
            else
                ref_html="<span class=\"no-img\">&mdash;</span>"
            fi

            if [ -n "$out_rel" ]; then
                out_html="<figure><a href=\"${out_rel}\" target=\"_blank\"><img src=\"${out_rel}\" alt=\"generated\"></a><figcaption>${out_mtime}</figcaption></figure>"
            else
                out_html="<span class=\"no-img\">&mdash;</span>"
            fi

            echo "      <tr>"
            echo "        <td class=\"col-name\">${name}</td>"
            echo "        <td class=\"col-type\">${type}</td>"
            echo "        <td class=\"${sc}\">${status}</td>"
            echo "        <td class=\"col-img\">${ref_html}</td>"
            echo "        <td class=\"col-img\">${out_html}</td>"
            echo "      </tr>"
        done

        cat <<'HTMLEND'
    </tbody>
  </table>
</body>
</html>
HTMLEND
    } > "$report"

    echo ""
    echo "HTML report: file://${report}"
}

run_parse_test() {
    local rib_file="$1"
    local name="$(basename "$rib_file" .rib)"

    # Skip shadow map generation files for parse test (they're tested via render)
    if [[ "$name" == *"_map"* ]]; then
        return
    fi

    if "$CATRIB" "$rib_file" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [parse] $name"
        ((PASS++)) || true
        record_test "$name" "parse" "PASS"
    else
        echo -e "${RED}FAIL${NC} [parse] $name"
        ((FAIL++)) || true
        record_test "$name" "parse" "FAIL"
    fi
}

run_shadow_map_generation() {
    # Generate shadow maps before running shadow render tests
    for shadow_map_rib in "$RIB_DIR"/shadow/*_map.rib; do
        [ -f "$shadow_map_rib" ] || continue
        local name="$(basename "$shadow_map_rib" _map.rib)"
        local shadow_map_out="$OUT_DIR/shadow/${name}.shd"
        mkdir -p "$(dirname "$shadow_map_out")"
        local temp_rib="$OUT_DIR/temp_${name}_map.rib"
        sed "s|Display.*|Display \"$shadow_map_out\" \"file\" \"z\"|" "$shadow_map_rib" > "$temp_rib"
        "$RENDER" "$temp_rib" 2>/dev/null
        rm -f "$temp_rib"
    done
}

run_render_test() {
    local rib_file="$1"
    local rel_path="${rib_file#$RIB_DIR/}"
    local name="$(basename "$rib_file" .rib)"
    local ref_file="$REF_DIR/${rel_path%.rib}.png"
    local out_file="$OUT_DIR/${rel_path%.rib}.png"

    # Skip shadow map generation RIB files (they output .shd not .png)
    if [[ "$name" == *"_map"* ]]; then
        return
    fi

    mkdir -p "$(dirname "$out_file")"

    # Create temp RIB with output redirected
    local temp_rib="$OUT_DIR/temp_$name.rib"
    # For shadow render tests, also update shadow map paths
    sed -e "s|Display.*|Display \"$out_file\" \"file\" \"rgba\"|" \
        -e "s|tests/shadow/\([^\"]*\.shd\)|$OUT_DIR/shadow/\1|g" \
        "$rib_file" > "$temp_rib"

    if ! "$RENDER" "$temp_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [render] $name (render failed)"
        ((FAIL++)) || true
        record_test "$name" "render" "FAIL" "" ""
        rm -f "$temp_rib"
        return
    fi
    rm -f "$temp_rib"

    if [ ! -f "$ref_file" ]; then
        echo -e "${YELLOW}SKIP${NC} [render] $name (no reference)"
        ((SKIP++)) || true
        record_test "$name" "render" "SKIP" "" "$out_file"
        return
    fi

    if diff -q "$out_file" "$ref_file" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC} [render] $name"
        ((PASS++)) || true
        record_test "$name" "render" "PASS" "$ref_file" "$out_file"
    else
        echo -e "${RED}FAIL${NC} [render] $name (output differs)"
        ((FAIL++)) || true
        record_test "$name" "render" "FAIL" "$ref_file" "$out_file"
    fi
}

run_roundtrip_test() {
    local rib_file="$1"
    local name="$(basename "$rib_file" .rib)"
    local out_rib="$OUT_DIR/roundtrip_$name.rib"

    # Skip shadow map generation files for roundtrip test
    if [[ "$name" == *"_map"* ]]; then
        return
    fi

    if ! "$CATRIB" "$rib_file" -o "$out_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [roundtrip] $name (catrib failed)"
        ((FAIL++)) || true
        record_test "$name" "roundtrip" "FAIL"
        return
    fi

    # Parse the output again
    if "$CATRIB" "$out_rib" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [roundtrip] $name"
        ((PASS++)) || true
        record_test "$name" "roundtrip" "PASS"
    else
        echo -e "${RED}FAIL${NC} [roundtrip] $name (re-parse failed)"
        ((FAIL++)) || true
        record_test "$name" "roundtrip" "FAIL"
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

# Pre-generate shadow maps for shadow tests
run_shadow_map_generation

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

generate_html_report

exit $FAIL
