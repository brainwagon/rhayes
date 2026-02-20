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

# One row per test key ("subdir/name"), with per-type status stored separately
declare -a ROW_KEYS=()
declare -A ROW_SEEN=()
declare -A ROW_PARSE=()
declare -A ROW_RENDER=()
declare -A ROW_ROUNDTRIP=()
declare -A ROW_REF_REL=()
declare -A ROW_OUT_REL=()
declare -A ROW_REF_MTIME=()
declare -A ROW_OUT_MTIME=()

RUN_TIMESTAMP="$(date '+%Y-%m-%d %H:%M:%S')"

# Colors for terminal output
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

ensure_key() {
    local key="$1"
    if [ -z "${ROW_SEEN[$key]+x}" ]; then
        ROW_KEYS+=("$key")
        ROW_SEEN[$key]=1
        ROW_PARSE[$key]=""
        ROW_RENDER[$key]=""
        ROW_ROUNDTRIP[$key]=""
        ROW_REF_REL[$key]=""
        ROW_OUT_REL[$key]=""
        ROW_REF_MTIME[$key]=""
        ROW_OUT_MTIME[$key]=""
    fi
}

record_test() {
    local key="$1" type="$2" status="$3"
    local ref_abs="${4:-}" out_abs="${5:-}"

    ensure_key "$key"

    case "$type" in
        parse)     ROW_PARSE[$key]="$status" ;;
        render)    ROW_RENDER[$key]="$status" ;;
        roundtrip) ROW_ROUNDTRIP[$key]="$status" ;;
    esac

    if [ -n "$ref_abs" ] && [ -f "$ref_abs" ]; then
        ROW_REF_REL[$key]="${ref_abs#$REF_DIR/}"
        ROW_REF_MTIME[$key]="$(get_file_mtime "$ref_abs")"
    fi
    if [ -n "$out_abs" ] && [ -f "$out_abs" ]; then
        ROW_OUT_REL[$key]="${out_abs#$OUT_DIR/}"
        ROW_OUT_MTIME[$key]="$(get_file_mtime "$out_abs")"
    fi
}

# Heroicons SVG for each status
status_icon() {
    case "$1" in
        PASS)
            # check-circle (solid, green)
            echo '<svg width="20" height="20" viewBox="0 0 24 24" fill="#16a34a" xmlns="http://www.w3.org/2000/svg"><path fill-rule="evenodd" d="M2.25 12c0-5.385 4.365-9.75 9.75-9.75s9.75 4.365 9.75 9.75-4.365 9.75-9.75 9.75S2.25 17.385 2.25 12zm13.36-1.814a.75.75 0 10-1.22-.872l-3.236 4.53L9.53 12.22a.75.75 0 00-1.06 1.06l2.25 2.25a.75.75 0 001.14-.094l3.75-5.25z" clip-rule="evenodd"/></svg>'
            ;;
        FAIL)
            # x-circle (solid, red)
            echo '<svg width="20" height="20" viewBox="0 0 24 24" fill="#dc2626" xmlns="http://www.w3.org/2000/svg"><path fill-rule="evenodd" d="M12 2.25c-5.385 0-9.75 4.365-9.75 9.75s4.365 9.75 9.75 9.75 9.75-4.365 9.75-9.75S17.385 2.25 12 2.25zm-1.72 6.97a.75.75 0 10-1.06 1.06L10.94 12l-1.72 1.72a.75.75 0 101.06 1.06L12 13.06l1.72 1.72a.75.75 0 101.06-1.06L13.06 12l1.72-1.72a.75.75 0 10-1.06-1.06L12 10.94l-1.72-1.72z" clip-rule="evenodd"/></svg>'
            ;;
        SKIP)
            echo '<span style="color:#9ca3af;font-size:16px" title="skipped">—</span>'
            ;;
        *)
            echo '<span style="color:#e5e7eb;font-size:16px">—</span>'
            ;;
    esac
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
    body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; font-size: 13px; }
    h1 { color: #111; margin-bottom: 4px; font-size: 1.3em; }
    .meta { color: #6b7280; margin-bottom: 6px; }
    .summary { margin-bottom: 14px; }
    .pass-count { color: #16a34a; font-weight: bold; }
    .fail-count { color: #dc2626; font-weight: bold; }
    .skip-count { color: #d97706; font-weight: bold; }
    table { border-collapse: collapse; width: 100%; background: white;
            box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
    th, td { border: 1px solid #e5e7eb; padding: 5px 8px; vertical-align: middle; }
    th { background: #f3f4f6; font-weight: 600; text-align: center; font-size: 11px;
         text-transform: uppercase; letter-spacing: 0.04em; color: #6b7280; }
    tr:hover td { background: #f9fafb; }
    .col-name { text-align: left; font-family: monospace; font-size: 12px;
                white-space: nowrap; color: #1f2937; }
    .col-icon { text-align: center; width: 44px; }
    .col-img  { text-align: center; width: 170px; }
    figure { margin: 0; }
    img { max-width: 150px; max-height: 110px; display: block; margin: 0 auto;
          border: 1px solid #e5e7eb; }
    figcaption { font-size: 10px; color: #9ca3af; margin-top: 2px; }
    .no-img { color: #d1d5db; font-size: 16px; }
  </style>
</head>
<body>
HTMLHEAD

        echo "  <h1>Rhayes Test Report</h1>"
        echo "  <p class=\"meta\">Run at: <strong>${RUN_TIMESTAMP}</strong></p>"
        echo "  <div class=\"summary\">"
        echo "    <span class=\"pass-count\">&#x2714; ${PASS} passed</span> &nbsp;"
        echo "    <span class=\"fail-count\">&#x2718; ${FAIL} failed</span> &nbsp;"
        echo "    <span class=\"skip-count\">&#x25CB; ${SKIP} skipped</span>"
        echo "  </div>"

        cat <<'HTMLTABLE'
  <table>
    <thead>
      <tr>
        <th style="text-align:left">Test</th>
        <th>Parse</th>
        <th>Render</th>
        <th>Roundtrip</th>
        <th>Reference</th>
        <th>Generated</th>
      </tr>
    </thead>
    <tbody>
HTMLTABLE

        for key in "${ROW_KEYS[@]}"; do
            local parse_icon render_icon roundtrip_icon
            parse_icon="$(status_icon "${ROW_PARSE[$key]}")"
            render_icon="$(status_icon "${ROW_RENDER[$key]}")"
            roundtrip_icon="$(status_icon "${ROW_ROUNDTRIP[$key]}")"

            local ref_rel="${ROW_REF_REL[$key]}"
            local out_rel="${ROW_OUT_REL[$key]}"

            local ref_html out_html
            if [ -n "$ref_rel" ]; then
                ref_html="<figure><a href=\"../reference/${ref_rel}\" target=\"_blank\"><img src=\"../reference/${ref_rel}\" alt=\"ref\"></a><figcaption>${ROW_REF_MTIME[$key]}</figcaption></figure>"
            else
                ref_html="<span class=\"no-img\">&mdash;</span>"
            fi

            if [ -n "$out_rel" ]; then
                out_html="<figure><a href=\"${out_rel}\" target=\"_blank\"><img src=\"${out_rel}\" alt=\"gen\"></a><figcaption>${ROW_OUT_MTIME[$key]}</figcaption></figure>"
            else
                out_html="<span class=\"no-img\">&mdash;</span>"
            fi

            echo "      <tr>"
            echo "        <td class=\"col-name\">${key}</td>"
            echo "        <td class=\"col-icon\">${parse_icon}</td>"
            echo "        <td class=\"col-icon\">${render_icon}</td>"
            echo "        <td class=\"col-icon\">${roundtrip_icon}</td>"
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
    local rel_path="${rib_file#$RIB_DIR/}"
    local key="${rel_path%.rib}"
    local name="$(basename "$rib_file" .rib)"

    # Skip shadow map generation files
    if [[ "$name" == *"_map"* ]]; then
        return
    fi

    if "$CATRIB" "$rib_file" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [parse] $key"
        ((PASS++)) || true
        record_test "$key" "parse" "PASS"
    else
        echo -e "${RED}FAIL${NC} [parse] $key"
        ((FAIL++)) || true
        record_test "$key" "parse" "FAIL"
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
    local key="${rel_path%.rib}"
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
    sed -e "s|Display.*|Display \"$out_file\" \"file\" \"rgba\"|" \
        -e "s|tests/shadow/\([^\"]*\.shd\)|$OUT_DIR/shadow/\1|g" \
        "$rib_file" > "$temp_rib"

    if ! "$RENDER" "$temp_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [render] $key (render failed)"
        ((FAIL++)) || true
        record_test "$key" "render" "FAIL" "" ""
        rm -f "$temp_rib"
        return
    fi
    rm -f "$temp_rib"

    if [ ! -f "$ref_file" ]; then
        echo -e "${YELLOW}SKIP${NC} [render] $key (no reference)"
        ((SKIP++)) || true
        record_test "$key" "render" "SKIP" "" "$out_file"
        return
    fi

    if diff -q "$out_file" "$ref_file" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${NC} [render] $key"
        ((PASS++)) || true
        record_test "$key" "render" "PASS" "$ref_file" "$out_file"
    else
        echo -e "${RED}FAIL${NC} [render] $key (output differs)"
        ((FAIL++)) || true
        record_test "$key" "render" "FAIL" "$ref_file" "$out_file"
    fi
}

run_roundtrip_test() {
    local rib_file="$1"
    local rel_path="${rib_file#$RIB_DIR/}"
    local key="${rel_path%.rib}"
    local name="$(basename "$rib_file" .rib)"
    local out_rib="$OUT_DIR/roundtrip_$name.rib"

    # Skip shadow map generation files for roundtrip test
    if [[ "$name" == *"_map"* ]]; then
        return
    fi

    if ! "$CATRIB" "$rib_file" -o "$out_rib" 2>/dev/null; then
        echo -e "${RED}FAIL${NC} [roundtrip] $key (catrib failed)"
        ((FAIL++)) || true
        record_test "$key" "roundtrip" "FAIL"
        return
    fi

    # Parse the output again
    if "$CATRIB" "$out_rib" -o /dev/null 2>/dev/null; then
        echo -e "${GREEN}PASS${NC} [roundtrip] $key"
        ((PASS++)) || true
        record_test "$key" "roundtrip" "PASS"
    else
        echo -e "${RED}FAIL${NC} [roundtrip] $key (re-parse failed)"
        ((FAIL++)) || true
        record_test "$key" "roundtrip" "FAIL"
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
