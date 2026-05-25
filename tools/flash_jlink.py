#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


DEFAULT_DEVICE = "GD32F303ZK"
DEFAULT_INTERFACE = "SWD"
DEFAULT_SPEED = 8000
DEFAULT_IMAGE = Path("build/rtthread.elf")
# DEFAULT_BIN_ADDR = "0x08000000"
DEFAULT_BIN_ADDR = "0x08004000"

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Flash firmware with SEGGER J-Link Commander."
    )
    parser.add_argument(
        "--file",
        default=str(DEFAULT_IMAGE),
        help=f"Image to flash, default: {DEFAULT_IMAGE}",
    )
    parser.add_argument(
        "--device",
        default=DEFAULT_DEVICE,
        help=f"J-Link device name, default: {DEFAULT_DEVICE}",
    )
    parser.add_argument(
        "--interface",
        default=DEFAULT_INTERFACE,
        choices=["SWD", "JTAG", "FINE", "ICSP", "SPI", "C2"],
        help=f"Debug interface, default: {DEFAULT_INTERFACE}",
    )
    parser.add_argument(
        "--speed",
        type=int,
        default=DEFAULT_SPEED,
        help=f"J-Link speed in kHz, default: {DEFAULT_SPEED}",
    )
    parser.add_argument(
        "--bin-addr",
        default=DEFAULT_BIN_ADDR,
        help=f"Load address when flashing a .bin file, default: {DEFAULT_BIN_ADDR}",
    )
    parser.add_argument(
        "--jlink",
        default="",
        help="Path to JLinkExe/JLink.exe, auto-detected by default",
    )
    parser.add_argument(
        "--sn",
        default="",
        help="Optional J-Link USB serial number",
    )
    parser.add_argument(
        "--jtagconf",
        default="-1,-1",
        help="JTAG chain position as IRPre,DRPre, default: -1,-1 (auto-detect)",
    )
    parser.add_argument(
        "--no-run",
        action="store_true",
        help="Do not start target after flashing",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the J-Link command and commander script without executing it",
    )
    parser.add_argument(
        "--erase",
        action="store_true",
        help="Erase target flash only and exit",
    )
    return parser.parse_args()


def resolve_jlink(jlink_arg: str) -> str:
    candidates = []
    if jlink_arg:
        candidates.append(jlink_arg)
    candidates.extend(["JLinkExe", "JLink.exe"])

    for candidate in candidates:
        if not candidate:
            continue
        candidate_path = Path(candidate)
        if candidate_path.exists():
            return str(candidate_path)

        found = shutil.which(candidate)
        if found:
            return found

    raise FileNotFoundError(
        "Cannot find JLinkExe/JLink.exe. Use --jlink to specify the executable."
    )


def build_program_script(image: Path, bin_addr: str, run_target: bool) -> str:
    lines = [
        "r",
        "h",
    ]

    suffix = image.suffix.lower()
    if suffix == ".bin":
        lines.append(f'loadbin "{image}", {bin_addr}')
    else:
        lines.append(f'loadfile "{image}"')

    lines.append("r")
    if run_target:
        lines.append("g")
    lines.append("qc")

    return "\n".join(lines) + "\n"


def build_erase_script() -> str:
    return "\n".join(
        [
            "r",
            "h",
            "erase",
            "qc",
        ]
    ) + "\n"


def main() -> int:
    args = parse_args()

    try:
        jlink = resolve_jlink(args.jlink)
    except FileNotFoundError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.erase:
        script_content = build_erase_script()
    else:
        image = Path(args.file).resolve()
        if not image.exists():
            print(f"Image not found: {image}", file=sys.stderr)
            return 1

        script_content = build_program_script(
            image=image,
            bin_addr=args.bin_addr,
            run_target=not args.no_run,
        )

    command = [
        jlink,
        "-device",
        args.device,
        "-if",
        args.interface,
        "-speed",
        str(args.speed),
        "-autoconnect",
        "1",
    ]

    if args.interface == "JTAG":
        command.extend(["-JTAGConf", args.jtagconf])

    if args.sn:
        command.extend(["-USB", args.sn])

    if args.dry_run:
        print("J-Link command:")
        print(" ".join(command + ["-CommanderScript", "<tempfile>"]))
        print("\nCommander script:")
        print(script_content, end="")
        return 0

    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as script_file:
        script_file.write(script_content)
        script_path = Path(script_file.name)

    try:
        full_command = command + ["-CommanderScript", str(script_path)]
        print("Running:", " ".join(full_command))
        result = subprocess.run(full_command)
        return result.returncode
    finally:
        script_path.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
