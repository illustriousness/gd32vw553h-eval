#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

OPENOCD_CFG="${OPENOCD_CFG:-${ROOT_DIR}/openocd_gdlink.cfg}"

RTT_ADDR="${RTT_ADDR:-0x20000000}"
RTT_SEARCH_SIZE="${RTT_SEARCH_SIZE:-0x10000}"
RTT_ID="${RTT_ID:-SEGGER RTT}"
RTT_PORT="${RTT_PORT:-19020}"

exec "openocd" \
    -f "${OPENOCD_CFG}" \
    -c "rtt setup ${RTT_ADDR} ${RTT_SEARCH_SIZE} \"${RTT_ID}\"" \
    -c "rtt start" \
    -c "rtt server start ${RTT_PORT} 0" \
    -c "resume"
