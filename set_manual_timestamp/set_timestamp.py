#!/usr/bin/env python3
"""
Set the last litter-box change timestamp in the toilet-timer NVS partition.

Usage:
    python3 set_timestamp.py "2026-03-13 11:00"
    python3 set_timestamp.py "2026-03-13 11:00" --flash /dev/cu.usbmodem*
    python3 set_timestamp.py now --flash /dev/cu.usbmodem*
"""

import argparse
import glob
import os
import struct
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

# NVS layout (from partition table)
NVS_SIZE_BYTES = 0x4000  # 16 K
NVS_PARTITION_NAME = "nvs"

# NVS keys (must match trigger.c / sntp.c)
TRIGGER_NAMESPACE = "trigger_info"
TRIGGER_KEY = "last_gpio4"
SNTP_NAMESPACE = "sntp_info"
SNTP_FIRST_SYNC_KEY = "first_sync"

NVS_GEN_REL = Path("components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py")
PARTTOOL_REL = Path("components/partition_table/parttool.py")


def find_idf_base() -> Path:
    """Locate the ESP-IDF installation directory."""
    # 1. IDF_PATH env var — set when the ESP-IDF environment is activated
    idf_env = os.environ.get("IDF_PATH")
    if idf_env:
        p = Path(idf_env)
        if (p / "components").is_dir():
            return p

    # 2. Search ~/.espressif/ for versioned installations (e.g. v5.5.2/esp-idf)
    espressif = Path.home() / ".espressif"
    if espressif.exists():
        candidates = [
            child / "esp-idf"
            for child in espressif.iterdir()
            if (child / "esp-idf" / "components").is_dir()
        ]
        if candidates:
            return max(candidates, key=lambda p: p.stat().st_mtime)

    print("ERROR: ESP-IDF installation not found.")
    print("       Activate the ESP-IDF environment or set the IDF_PATH variable.")
    sys.exit(1)


def find_tool(rel: Path, name: str) -> Path:
    idf = find_idf_base()
    tool = idf / rel
    if not tool.exists():
        print(f"ERROR: {name} not found at {tool}")
        sys.exit(1)
    return tool


def find_idf_python() -> str:
    """Return the Python interpreter that has esp_idf_nvs_partition_gen installed."""
    # Prefer IDF_PYTHON env var set by idf.py
    idf_python = os.environ.get("IDF_PYTHON_ENV_PATH")
    if idf_python:
        candidate = Path(idf_python) / "bin/python3"
        if candidate.exists():
            return str(candidate)

    # Search ~/.espressif/python_env for a venv that has the module
    env_dir = Path.home() / ".espressif/python_env"
    if env_dir.exists():
        for venv in sorted(env_dir.iterdir(), reverse=True):
            py = venv / "bin/python3"
            if py.exists():
                result = subprocess.run(
                    [str(py), "-c", "import esp_idf_nvs_partition_gen"],
                    capture_output=True
                )
                if result.returncode == 0:
                    return str(py)

    # Fall back to current interpreter (works if run inside idf.py environment)
    return sys.executable


def parse_datetime(value: str) -> datetime:
    if value.lower() == "now":
        return datetime.now()
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            return datetime.strptime(value, fmt)
        except ValueError:
            continue
    print(f"ERROR: Cannot parse date '{value}'")
    print("       Use format: 'YYYY-MM-DD HH:MM' or 'YYYY-MM-DD HH:MM:SS' or 'now'")
    sys.exit(1)


def timestamp_to_blob(ts: int) -> str:
    """Pack Unix timestamp as 8-byte little-endian int64, return as uppercase hex string."""
    return struct.pack("<q", ts).hex().upper()


def generate_nvs_bin(ts: int, out_path: str) -> None:
    nvs_gen = find_tool(NVS_GEN_REL, "nvs_partition_gen.py")

    blob_hex = timestamp_to_blob(ts)

    csv_content = (
        "key,type,encoding,value\n"
        f"{TRIGGER_NAMESPACE},namespace,,\n"
        f"{TRIGGER_KEY},data,hex2bin,{blob_hex}\n"
        f"{SNTP_NAMESPACE},namespace,,\n"
        f"{SNTP_FIRST_SYNC_KEY},data,hex2bin,01\n"
    )

    idf_python = find_idf_python()

    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as f:
        f.write(csv_content)
        csv_path = f.name

    try:
        result = subprocess.run(
            [idf_python, str(nvs_gen), "generate", csv_path, out_path, hex(NVS_SIZE_BYTES)],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print("ERROR: nvs_partition_gen.py failed:")
            print(result.stderr)
            sys.exit(1)
    finally:
        os.unlink(csv_path)


def flash_nvs(bin_path: str, port: str) -> None:
    parttool = find_tool(PARTTOOL_REL, "parttool.py")

    # Resolve glob in port (e.g. /dev/cu.usbmodem*)
    if "*" in port or "?" in port:
        matches = glob.glob(port)
        if not matches:
            print(f"ERROR: No device found matching '{port}'")
            sys.exit(1)
        port = matches[0]

    idf_python = find_idf_python()
    print(f"Flashing to {port} ...")
    result = subprocess.run(
        [idf_python, str(parttool), "--port", port,
         "write_partition", f"--partition-name={NVS_PARTITION_NAME}", f"--input={bin_path}"],
        capture_output=False
    )
    if result.returncode != 0:
        print("ERROR: parttool.py failed.")
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Set the toilet-timer last-change timestamp in NVS.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 set_timestamp.py "2026-03-13 11:00"
  python3 set_timestamp.py "2026-03-13 11:00" --flash /dev/cu.usbmodem*
  python3 set_timestamp.py now --flash /dev/cu.usbmodem*

The datetime is interpreted in your LOCAL timezone (which should match the
timezone configured on the device via CONFIG_SNTP_TIMEZONE).
        """
    )
    parser.add_argument(
        "datetime",
        help="Target date/time: 'YYYY-MM-DD HH:MM', 'YYYY-MM-DD HH:MM:SS', or 'now'",
    )
    parser.add_argument(
        "--flash", metavar="PORT",
        help="Serial port to flash the NVS partition to (e.g. /dev/cu.usbmodem*). "
             "If omitted, only the .bin file is generated.",
    )
    parser.add_argument(
        "--output", metavar="FILE", default="nvs_timestamp.bin",
        help="Output .bin file path (default: nvs_timestamp.bin)",
    )
    args = parser.parse_args()

    dt = parse_datetime(args.datetime)
    ts = int(dt.timestamp())

    print(f"Target datetime : {dt.strftime('%Y-%m-%d %H:%M:%S')} (local)")
    print(f"Unix timestamp  : {ts}")
    print(f"Blob (LE int64) : {timestamp_to_blob(ts)}")

    out_path = os.path.abspath(args.output)
    generate_nvs_bin(ts, out_path)
    print(f"NVS binary      : {out_path}")

    if args.flash:
        flash_nvs(out_path, args.flash)
        print("Done. Power-cycle or reset the device.")
    else:
        print()
        print("Binary ready. To flash, run:")
        print(f"  python3 set_timestamp.py \"{args.datetime}\" --flash /dev/cu.usbmodem*")
        print("Or manually:")
        parttool = find_tool(PARTTOOL_REL, "parttool.py")
        print(f"  python3 {parttool} --port PORT write_partition "
              f"--partition-name={NVS_PARTITION_NAME} --input={out_path}")


if __name__ == "__main__":
    main()
