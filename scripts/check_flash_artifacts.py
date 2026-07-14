#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import struct
import sys
from pathlib import Path


DEFAULT_ENV = "waveshare_single_can_standalone"
DEFAULT_PARTITION = "partitions_16mb_ota_4096k_nvs64.csv"
FLASH_SIZE_BYTES = 16 * 1024 * 1024
APP_OFFSET = 0x20000
OTA_DATA_OFFSET = 0x19000
PARTITION_OFFSET = 0x8000
OTA_DATA_SIZE = 0x2000

TYPE_VALUES = {"app": 0x00, "data": 0x01}
SUBTYPE_VALUES = {
    ("data", "ota"): 0x00,
    ("data", "nvs"): 0x02,
    ("data", "coredump"): 0x03,
    ("app", "ota_0"): 0x10,
    ("app", "ota_1"): 0x11,
    ("data", "spiffs"): 0x82,
}
FLASH_SIZE_CODES = {0: 1, 1: 2, 2: 4, 3: 8, 4: 16, 5: 32, 6: 64, 7: 128}


class CheckError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise CheckError(message)


def read_bytes(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except FileNotFoundError:
        fail(f"missing required file: {path}")
    except OSError as exc:
        fail(f"could not read {path}: {exc}")


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        fail(f"missing required file: {path}")
    except OSError as exc:
        fail(f"could not read {path}: {exc}")


def read_json(path: Path) -> dict:
    try:
        return json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON in {path}: {exc}")


def parse_size(raw: str) -> int:
    value = raw.strip()
    suffix = value[-1:].upper()
    if suffix in {"K", "M"}:
        multiplier = 1024 if suffix == "K" else 1024 * 1024
        return int(value[:-1], 0) * multiplier
    return int(value, 0)


def parse_partition_csv(path: Path) -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    for row in csv.reader(io.StringIO(read_text(path))):
        if not row or row[0].lstrip().startswith("#"):
            continue
        if len(row) != 6:
            fail(f"partition CSV row must have 6 columns: {row}")
        name, kind, subtype, offset, size, flags = [item.strip() for item in row]
        if kind not in TYPE_VALUES or (kind, subtype) not in SUBTYPE_VALUES:
            fail(f"unsupported partition type/subtype: {kind}/{subtype}")
        rows.append(
            {
                "name": name,
                "type": TYPE_VALUES[kind],
                "subtype": SUBTYPE_VALUES[(kind, subtype)],
                "offset": int(offset, 0),
                "size": parse_size(size),
                "flags": int(flags or "0", 0),
            }
        )
    if not rows:
        fail(f"partition CSV is empty: {path}")
    return rows


def parse_partition_binary(path: Path) -> list[dict[str, int | str]]:
    data = read_bytes(path)
    rows: list[dict[str, int | str]] = []
    for start in range(0, len(data) - 31, 32):
        record = data[start : start + 32]
        magic = struct.unpack_from("<H", record)[0]
        if magic in {0xFFFF, 0xEBEB}:
            break
        if magic != 0x50AA:
            fail(f"partition binary has invalid magic at {start:#x}: {magic:#06x}")
        _, kind, subtype, offset, size, label, flags = struct.unpack(
            "<HBBII16sI", record
        )
        rows.append(
            {
                "name": label.split(b"\0", 1)[0].decode("ascii"),
                "type": kind,
                "subtype": subtype,
                "offset": offset,
                "size": size,
                "flags": flags,
            }
        )
    if not rows:
        fail(f"partition binary contains no entries: {path}")
    return rows


def check_partition_layout(rows: list[dict[str, int | str]]) -> None:
    previous_end = PARTITION_OFFSET + 0x1000
    for row in sorted(rows, key=lambda item: int(item["offset"])):
        offset = int(row["offset"])
        size = int(row["size"])
        if offset < previous_end:
            fail(f"partition {row['name']} overlaps previous region")
        if offset + size > FLASH_SIZE_BYTES:
            fail(f"partition {row['name']} extends past 16MB")
        previous_end = offset + size


def check_text_config(path: Path) -> None:
    text = read_text(path)
    required = (
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
        'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
        "CONFIG_PARTITION_TABLE_CUSTOM=y",
        f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{DEFAULT_PARTITION}"',
        f'CONFIG_PARTITION_TABLE_FILENAME="{DEFAULT_PARTITION}"',
    )
    for token in required:
        if token not in text:
            fail(f"{path} must contain {token} for 16MB custom partition geometry")
    forbidden = (
        "CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y",
        "CONFIG_PARTITION_TABLE_SINGLE_APP=y",
        'CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"',
    )
    for token in forbidden:
        if token in text:
            fail(f"{path} contains forbidden 2MB/single-app setting: {token}")


def check_header_config(path: Path) -> None:
    text = read_text(path)
    required = (
        '#define CONFIG_ESPTOOLPY_FLASHSIZE "16MB"',
        "#define CONFIG_ESPTOOLPY_FLASHSIZE_16MB 1",
        "#define CONFIG_PARTITION_TABLE_CUSTOM 1",
        f'#define CONFIG_PARTITION_TABLE_CUSTOM_FILENAME "{DEFAULT_PARTITION}"',
        f'#define CONFIG_PARTITION_TABLE_FILENAME "{DEFAULT_PARTITION}"',
    )
    for token in required:
        if token not in text:
            fail(f"{path} must select 16MB custom partition geometry: missing {token}")
    if "CONFIG_PARTITION_TABLE_SINGLE_APP 1" in text:
        fail(f"{path} selects single-app instead of custom partition")


def check_json_config(path: Path) -> None:
    data = read_json(path)
    if data.get("ESPTOOLPY_FLASHSIZE") != "16MB":
        fail(f"{path} must report ESPTOOLPY_FLASHSIZE=16MB")
    if data.get("ESPTOOLPY_FLASHSIZE_16MB") is not True:
        fail(f"{path} must enable ESPTOOLPY_FLASHSIZE_16MB")
    if data.get("PARTITION_TABLE_CUSTOM") is not True:
        fail(f"{path} must enable custom partition table")
    if data.get("PARTITION_TABLE_CUSTOM_FILENAME") != DEFAULT_PARTITION:
        fail(f"{path} custom partition filename must be {DEFAULT_PARTITION}")
    if data.get("PARTITION_TABLE_FILENAME") != DEFAULT_PARTITION:
        fail(f"{path} partition filename must be {DEFAULT_PARTITION}")
    if data.get("PARTITION_TABLE_SINGLE_APP") is True:
        fail(f"{path} selects single-app instead of custom partition")


def normalize_offset(raw: str | int) -> int:
    return int(raw, 0) if isinstance(raw, str) else int(raw)


def check_flasher_args(path: Path) -> None:
    data = read_json(path)
    if data.get("flash_settings", {}).get("flash_size") != "16MB":
        fail(f"{path} flash size must be 16MB")
    args = data.get("write_flash_args", [])
    if "--flash-size" not in args:
        fail(f"{path} write_flash_args must include --flash-size 16MB")
    size_index = args.index("--flash-size") + 1
    if size_index >= len(args) or args[size_index] != "16MB":
        fail(f"{path} write_flash_args must include --flash-size 16MB")

    expected_roles = {
        "bootloader": 0x0,
        "partition-table": PARTITION_OFFSET,
        "otadata": OTA_DATA_OFFSET,
        "app": APP_OFFSET,
    }
    for role, expected in expected_roles.items():
        actual = data.get(role, {}).get("offset")
        if actual is None or normalize_offset(actual) != expected:
            fail(f"{path} {role} offset must be {expected:#x}")

    files = {normalize_offset(key): value for key, value in data.get("flash_files", {}).items()}
    for offset in expected_roles.values():
        if offset not in files:
            fail(f"{path} flash_files must include {offset:#x}")
    if 0x10000 in files:
        fail(f"{path} must not place the app at 0x10000; expected 0x20000")


def check_image_header(path: Path) -> None:
    data = read_bytes(path)
    if len(data) < 4 or data[0] != 0xE9:
        fail(f"{path} is not an ESP image")
    size_code = data[3] >> 4
    size_mb = FLASH_SIZE_CODES.get(size_code)
    if size_mb != 16:
        fail(f"{path} image header must declare 16MB, found {size_mb or 'unknown'}MB")


def compare_partitions(expected: list[dict[str, int | str]], actual: list[dict[str, int | str]]) -> None:
    if len(actual) != len(expected):
        fail(f"partition binary entry count differs: expected {len(expected)}, found {len(actual)}")
    for index, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            fail(f"partition binary mismatch at entry {index}: expected {want}, found {got}")


def check_generated(root: Path, build: Path, partition_csv: Path) -> tuple[int, int]:
    check_text_config(root / f"sdkconfig.{DEFAULT_ENV}")
    for config_dir in (build / "config", build / "bootloader" / "config"):
        check_header_config(config_dir / "sdkconfig.h")
        check_json_config(config_dir / "sdkconfig.json")
    check_flasher_args(build / "flasher_args.json")

    check_image_header(build / "bootloader.bin")
    check_image_header(build / "firmware.bin")

    expected = parse_partition_csv(partition_csv)
    actual = parse_partition_binary(build / "partitions.bin")
    check_partition_layout(expected)
    check_partition_layout(actual)
    compare_partitions(expected, actual)

    ota_size = (build / "ota_data_initial.bin").stat().st_size
    if ota_size != OTA_DATA_SIZE:
        fail(f"ota_data_initial.bin must be 8192 bytes, found {ota_size}")

    app0 = next((row for row in expected if row["name"] == "app0"), None)
    if app0 is None:
        fail("partition CSV is missing app0")
    firmware_size = (build / "firmware.bin").stat().st_size
    app_size = int(app0["size"])
    if firmware_size <= 0 or firmware_size > app_size:
        fail(f"firmware size {firmware_size} exceeds app slot {app_size}")

    print(
        "flash artifact check passed: "
        f"firmware={firmware_size} bytes, app slot remaining={app_size - firmware_size} bytes"
    )
    return firmware_size, app_size


def parse_checksums(path: Path) -> dict[str, str]:
    checksums: dict[str, str] = {}
    for line in read_text(path).splitlines():
        parts = line.split()
        if len(parts) != 2:
            fail(f"invalid SHA256SUMS line: {line!r}")
        digest, name = parts
        checksums[name.lstrip("*")] = digest.lower()
    return checksums


def sha256(path: Path) -> str:
    return hashlib.sha256(read_bytes(path)).hexdigest()


def check_release(release: Path, version: str) -> None:
    asset_names = (
        "bootloader.bin",
        "partitions.bin",
        "ota_data_initial.bin",
        "firmware.bin",
        "firmware-waveshare-single-can.bin",
        "merged-flash.bin",
        "flash.sh",
        "SHA256SUMS",
    )
    for name in asset_names:
        if not (release / name).is_file():
            fail(f"release bundle missing {name}")

    if read_bytes(release / "firmware.bin") != read_bytes(
        release / "firmware-waveshare-single-can.bin"
    ):
        fail("firmware-waveshare-single-can.bin must match firmware.bin")

    script = read_text(release / "flash.sh")
    if f"v{version}-atlas-single-can" not in script:
        fail(f"flash.sh must identify v{version}-atlas-single-can")
    for offset in ("0x0", "0x8000", "0x19000", "0x20000"):
        if offset not in script:
            fail(f"flash.sh must contain release offset {offset}")

    expected_checksums = parse_checksums(release / "SHA256SUMS")
    expected_names = set(asset_names) - {"SHA256SUMS"}
    if set(expected_checksums) != expected_names:
        fail("SHA256SUMS must cover all seven release assets")
    for name, expected in expected_checksums.items():
        if sha256(release / name) != expected:
            fail(f"SHA256SUMS mismatch for {name}")

    merged = read_bytes(release / "merged-flash.bin")
    if len(merged) > FLASH_SIZE_BYTES:
        fail(f"merged-flash.bin exceeds 16MB: {len(merged)} bytes")
    check_image_header(release / "merged-flash.bin")
    bootloader = read_bytes(release / "bootloader.bin")
    merged_bootloader = merged[: len(bootloader)]
    if (
        merged_bootloader[:2] != bootloader[:2]
        or merged_bootloader[4:] != bootloader[4:]
    ):
        fail("merged-flash.bin does not contain bootloader.bin at 0x0")

    for offset, name in (
        (PARTITION_OFFSET, "partitions.bin"),
        (OTA_DATA_OFFSET, "ota_data_initial.bin"),
        (APP_OFFSET, "firmware.bin"),
    ):
        payload = read_bytes(release / name)
        if merged[offset : offset + len(payload)] != payload:
            fail(f"merged-flash.bin does not contain {name} at {offset:#x}")

    print(f"release bundle check passed: v{version}, assets=8")


def parse_args() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Verify Waveshare 16MB flash artifacts")
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=root / ".pio" / "build" / DEFAULT_ENV,
    )
    parser.add_argument(
        "--partition-csv",
        type=Path,
        default=root / DEFAULT_PARTITION,
    )
    parser.add_argument("--release-dir", type=Path)
    parser.add_argument("--version")
    parser.add_argument(
        "--release-only",
        action="store_true",
        help="check a packaged release without requiring the original build tree",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        if args.release_only and args.release_dir is None:
            fail("--release-only requires --release-dir")
        if not args.release_only:
            check_generated(
                args.root.resolve(),
                args.build_dir.resolve(),
                args.partition_csv.resolve(),
            )
        if args.release_dir is not None:
            version = args.version or read_text(args.root / "VERSION").strip()
            check_release(args.release_dir.resolve(), version)
    except (CheckError, OSError) as exc:
        print(f"flash artifact check failed: {exc}", file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
