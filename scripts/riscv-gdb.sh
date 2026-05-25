#!/usr/bin/env bash
set -euo pipefail

if [ -n "${RTT_EXEC_PATH:-}" ]; then
    export PATH="${RTT_EXEC_PATH}:${PATH}"
fi

if [ -n "${GDB_BIN:-}" ]; then
    exec "${GDB_BIN}" "$@"
fi

for gdb in riscv64-unknown-elf-gdb gdb-multiarch; do
    if command -v "${gdb}" >/dev/null 2>&1; then
        exec "${gdb}" "$@"
    fi
done

echo "No RISC-V GDB found. Install with: sudo apt install gdb-multiarch" >&2
exit 1
