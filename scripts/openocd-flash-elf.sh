#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

OPENOCD_CFG="${OPENOCD_CFG:-${ROOT_DIR}/openocd_gdlink.cfg}"
ELF_FILE="${1:-${ROOT_DIR}/build/rtthread.elf}"

if [ ! -f "${ELF_FILE}" ]; then
    echo "ELF file not found: ${ELF_FILE}" >&2
    exit 1
fi

exec "openocd" \
    -f "${OPENOCD_CFG}" \
    -c "program ${ELF_FILE} verify reset exit"
