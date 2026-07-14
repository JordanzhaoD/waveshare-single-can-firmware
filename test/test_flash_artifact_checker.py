import csv
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "scripts" / "check_flash_artifacts.py"
PARTITION_FILE = "partitions_16mb_ota_4096k_nvs64.csv"


class FlashArtifactFixture:
    def __init__(self, root: Path) -> None:
        self.root = root
        self.build = root / ".pio" / "build" / "waveshare_single_can_standalone"
        self.release = root / ".pio" / "release-assets-v1.0.9"
        self.sdkconfig = root / "sdkconfig.waveshare_single_can_standalone"
        self.partition_csv = root / PARTITION_FILE

    def create(self) -> None:
        self.build.mkdir(parents=True)
        (self.build / "config").mkdir()
        (self.build / "bootloader" / "config").mkdir(parents=True)
        self.partition_csv.write_text(
            "# Name, Type, SubType, Offset, Size, Flags\n"
            "nvs,data,nvs,0x9000,0x10000,\n"
            "otadata,data,ota,0x19000,0x2000,\n"
            "app0,app,ota_0,0x20000,0x400000,\n"
            "app1,app,ota_1,0x420000,0x400000,\n"
            "spiffs,data,spiffs,0x820000,0x7C0000,\n"
            "coredump,data,coredump,0xFE0000,0x20000,\n",
            encoding="utf-8",
        )
        self.write_configs()
        self.write_flasher_args()
        (self.build / "bootloader.bin").write_bytes(self.image_bytes(b"boot" * 20))
        (self.build / "firmware.bin").write_bytes(self.image_bytes(b"firmware"))
        (self.build / "partitions.bin").write_bytes(self.partition_binary())
        (self.build / "ota_data_initial.bin").write_bytes(b"\xff" * 0x2000)

    def write_configs(self, flash_size: str = "16MB", custom: bool = True) -> None:
        enabled_size = "16MB" if flash_size == "16MB" else "2MB"
        text = (
            f"CONFIG_ESPTOOLPY_FLASHSIZE_{enabled_size}=y\n"
            f'CONFIG_ESPTOOLPY_FLASHSIZE="{flash_size}"\n'
        )
        if custom:
            text += (
                "CONFIG_PARTITION_TABLE_CUSTOM=y\n"
                f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{PARTITION_FILE}"\n'
                f'CONFIG_PARTITION_TABLE_FILENAME="{PARTITION_FILE}"\n'
            )
        else:
            text += (
                "CONFIG_PARTITION_TABLE_SINGLE_APP=y\n"
                'CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"\n'
            )
        self.sdkconfig.write_text(text, encoding="utf-8")

        header = (
            f'#define CONFIG_ESPTOOLPY_FLASHSIZE "{flash_size}"\n'
            f"#define CONFIG_ESPTOOLPY_FLASHSIZE_{enabled_size} 1\n"
        )
        data = {
            "ESPTOOLPY_FLASHSIZE": flash_size,
            f"ESPTOOLPY_FLASHSIZE_{enabled_size}": True,
        }
        if custom:
            header += (
                "#define CONFIG_PARTITION_TABLE_CUSTOM 1\n"
                f'#define CONFIG_PARTITION_TABLE_CUSTOM_FILENAME "{PARTITION_FILE}"\n'
                f'#define CONFIG_PARTITION_TABLE_FILENAME "{PARTITION_FILE}"\n'
            )
            data.update(
                {
                    "PARTITION_TABLE_CUSTOM": True,
                    "PARTITION_TABLE_CUSTOM_FILENAME": PARTITION_FILE,
                    "PARTITION_TABLE_FILENAME": PARTITION_FILE,
                    "PARTITION_TABLE_SINGLE_APP": False,
                }
            )
        else:
            header += (
                "#define CONFIG_PARTITION_TABLE_SINGLE_APP 1\n"
                '#define CONFIG_PARTITION_TABLE_FILENAME "partitions_singleapp.csv"\n'
            )
            data.update(
                {
                    "PARTITION_TABLE_CUSTOM": False,
                    "PARTITION_TABLE_FILENAME": "partitions_singleapp.csv",
                    "PARTITION_TABLE_SINGLE_APP": True,
                }
            )
        for directory in (self.build / "config", self.build / "bootloader" / "config"):
            (directory / "sdkconfig.h").write_text(header, encoding="utf-8")
            (directory / "sdkconfig.json").write_text(json.dumps(data), encoding="utf-8")

    def write_flasher_args(self, app_offset: str = "0x20000") -> None:
        payload = {
            "write_flash_args": [
                "--flash-mode",
                "dio",
                "--flash-size",
                "16MB",
                "--flash-freq",
                "80m",
            ],
            "flash_settings": {
                "flash_mode": "dio",
                "flash_size": "16MB",
                "flash_freq": "80m",
            },
            "flash_files": {
                "0x0": "bootloader/bootloader.bin",
                "0x8000": "partition_table/partition-table.bin",
                "0x19000": "ota_data_initial.bin",
                app_offset: "ev_open_can_tools.bin",
            },
            "bootloader": {"offset": "0x0"},
            "partition-table": {"offset": "0x8000"},
            "otadata": {"offset": "0x19000"},
            "app": {"offset": app_offset},
        }
        (self.build / "flasher_args.json").write_text(
            json.dumps(payload), encoding="utf-8"
        )

    @staticmethod
    def image_bytes(payload: bytes) -> bytes:
        return bytes([0xE9, 1, 2, 0x4F]) + payload

    def partition_binary(self, app0_offset: int = 0x20000) -> bytes:
        type_map = {"app": 0x00, "data": 0x01}
        subtype_map = {
            ("data", "ota"): 0x00,
            ("data", "nvs"): 0x02,
            ("data", "coredump"): 0x03,
            ("app", "ota_0"): 0x10,
            ("app", "ota_1"): 0x11,
            ("data", "spiffs"): 0x82,
        }
        records = bytearray()
        with self.partition_csv.open(newline="", encoding="utf-8") as handle:
            for row in csv.reader(handle):
                if not row or row[0].lstrip().startswith("#"):
                    continue
                name, kind, subtype, offset, size, flags = [item.strip() for item in row]
                value_offset = int(offset, 0)
                if name == "app0":
                    value_offset = app0_offset
                label = name.encode("ascii").ljust(16, b"\0")
                records += struct.pack(
                    "<HBBII16sI",
                    0x50AA,
                    type_map[kind],
                    subtype_map[(kind, subtype)],
                    value_offset,
                    int(size, 0),
                    label,
                    int(flags or "0", 0),
                )
        records += b"\xff" * 32
        return bytes(records).ljust(0xC00, b"\xff")

    def create_release(self, version: str = "1.0.9") -> None:
        self.release.mkdir(parents=True)
        for name in (
            "bootloader.bin",
            "partitions.bin",
            "ota_data_initial.bin",
            "firmware.bin",
        ):
            (self.release / name).write_bytes((self.build / name).read_bytes())
        (self.release / "firmware-waveshare-single-can.bin").write_bytes(
            (self.build / "firmware.bin").read_bytes()
        )
        flash_script = (
            f"# Waveshare v{version}-atlas-single-can\n"
            "# merged flash overwrites NVS; use --split to preserve NVS/SPIFFS\n"
            "0x0 bootloader.bin\n0x8000 partitions.bin\n"
            "0x19000 ota_data_initial.bin\n0x20000 firmware.bin\n"
        )
        (self.release / "flash.sh").write_text(flash_script, encoding="utf-8")

        merged_size = 0x20000 + (self.release / "firmware.bin").stat().st_size
        merged = bytearray(b"\xff" * merged_size)
        for offset, name in (
            (0x0, "bootloader.bin"),
            (0x8000, "partitions.bin"),
            (0x19000, "ota_data_initial.bin"),
            (0x20000, "firmware.bin"),
        ):
            data = (self.release / name).read_bytes()
            merged[offset : offset + len(data)] = data
        (self.release / "merged-flash.bin").write_bytes(merged)
        self.write_checksums()

    def write_checksums(self) -> None:
        names = (
            "bootloader.bin",
            "partitions.bin",
            "ota_data_initial.bin",
            "firmware.bin",
            "firmware-waveshare-single-can.bin",
            "merged-flash.bin",
            "flash.sh",
        )
        lines = []
        for name in names:
            digest = hashlib.sha256((self.release / name).read_bytes()).hexdigest()
            lines.append(f"{digest}  {name}")
        (self.release / "SHA256SUMS").write_text("\n".join(lines) + "\n")


class FlashArtifactCheckerTest(unittest.TestCase):
    def run_checker(
        self,
        fixture: FlashArtifactFixture,
        release: bool = False,
        release_only: bool = False,
    ):
        args = [
            sys.executable,
            str(CHECKER),
            "--root",
            str(fixture.root),
            "--build-dir",
            str(fixture.build),
            "--partition-csv",
            str(fixture.partition_csv),
        ]
        if release:
            args.extend(["--release-dir", str(fixture.release), "--version", "1.0.9"])
        if release_only:
            args.append("--release-only")
        return subprocess.run(args, capture_output=True, text=True)

    def with_fixture(self):
        temp = tempfile.TemporaryDirectory()
        fixture = FlashArtifactFixture(Path(temp.name))
        fixture.create()
        return temp, fixture

    def test_accepts_complete_16mb_generated_artifacts(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            result = self.run_checker(fixture)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("flash artifact check passed", result.stdout)

    def test_rejects_generated_sdkconfig_with_2mb(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.write_configs(flash_size="2MB")
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("16MB", result.stderr)

    def test_rejects_single_app_partition_selection(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.write_configs(custom=False)
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("custom partition", result.stderr)

    def test_rejects_flasher_args_app_at_0x10000(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.write_flasher_args(app_offset="0x10000")
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("0x20000", result.stderr)

    def test_rejects_image_header_not_16mb(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            (fixture.build / "firmware.bin").write_bytes(bytes([0xE9, 1, 2, 0x2F]))
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("16MB", result.stderr)

    def test_accepts_partition_binary_md5_record(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            partition = bytearray((fixture.build / "partitions.bin").read_bytes())
            partition[0xC0:0xE0] = struct.pack("<H", 0xEBEB) + b"\0" * 30
            (fixture.build / "partitions.bin").write_bytes(partition)
            result = self.run_checker(fixture)
            self.assertEqual(0, result.returncode, result.stderr)

    def test_rejects_partition_binary_mismatch(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            (fixture.build / "partitions.bin").write_bytes(
                fixture.partition_binary(app0_offset=0x10000)
            )
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("partition", result.stderr)

    def test_rejects_wrong_ota_data_size(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            (fixture.build / "ota_data_initial.bin").write_bytes(b"\xff" * 16)
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("8192", result.stderr)

    def test_rejects_firmware_larger_than_app_slot(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            oversized = fixture.image_bytes(b"\xff" * (0x400000 - 3))
            (fixture.build / "firmware.bin").write_bytes(oversized)
            result = self.run_checker(fixture)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("app slot", result.stderr)

    def test_accepts_complete_release_bundle(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.create_release()
            result = self.run_checker(fixture, release=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("release bundle check passed", result.stdout)

    def test_accepts_release_bundle_without_build_tree(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.create_release()
            shutil.rmtree(fixture.build)
            fixture.sdkconfig.unlink()
            result = self.run_checker(fixture, release=True, release_only=True)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertIn("release bundle check passed", result.stdout)

    def test_accepts_esptool_rewritten_merged_header_frequency(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.create_release()
            merged = bytearray((fixture.release / "merged-flash.bin").read_bytes())
            merged[3] = (merged[3] & 0xF0) | 0x00
            bootloader_size = (fixture.release / "bootloader.bin").stat().st_size
            merged[bootloader_size - 32 : bootloader_size] = b"\xaa" * 32
            (fixture.release / "merged-flash.bin").write_bytes(merged)
            fixture.write_checksums()
            result = self.run_checker(fixture, release=True)
            self.assertEqual(0, result.returncode, result.stderr)

    def test_rejects_release_checksum_mismatch(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.create_release()
            flash_script = (fixture.release / "flash.sh").read_text(encoding="utf-8")
            (fixture.release / "flash.sh").write_text(
                flash_script + "# corrupted after checksum generation\n",
                encoding="utf-8",
            )
            result = self.run_checker(fixture, release=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("SHA256SUMS", result.stderr)

    def test_rejects_merged_release_offset_mismatch(self) -> None:
        temp, fixture = self.with_fixture()
        with temp:
            fixture.create_release()
            merged = bytearray((fixture.release / "merged-flash.bin").read_bytes())
            merged[0x20000] ^= 0xFF
            (fixture.release / "merged-flash.bin").write_bytes(merged)
            fixture.write_checksums()
            result = self.run_checker(fixture, release=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn("0x20000", result.stderr)


if __name__ == "__main__":
    unittest.main()
