import csv
import io
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARTITION_FILE = "partitions_16mb_ota_4096k_nvs64.csv"
FLASH_SIZE = 0x1000000


class FlashGeometryContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        cls.sdkconfig = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        cls.partition_text = (ROOT / PARTITION_FILE).read_text(encoding="utf-8")

    def standalone_env(self) -> str:
        match = re.search(
            r"^\[env:waveshare_single_can_standalone\]\n(.*?)(?=^\[|\Z)",
            self.platformio,
            re.MULTILINE | re.DOTALL,
        )
        self.assertIsNotNone(match)
        return match.group(1)

    def partitions(self) -> list[dict[str, int | str]]:
        rows = []
        for row in csv.reader(io.StringIO(self.partition_text)):
            if not row or row[0].lstrip().startswith("#"):
                continue
            name, kind, subtype, offset, size, flags = [item.strip() for item in row]
            rows.append(
                {
                    "name": name,
                    "type": kind,
                    "subtype": subtype,
                    "offset": int(offset, 0),
                    "size": int(size, 0),
                    "flags": flags,
                }
            )
        return rows

    def test_platformio_declares_16mb_custom_partition(self) -> None:
        env = self.standalone_env()
        self.assertRegex(env, r"(?m)^board_build\.flash_size\s*=\s*16MB\s*$")
        self.assertRegex(
            env,
            rf"(?m)^board_build\.partitions\s*=\s*{re.escape(PARTITION_FILE)}\s*$",
        )

    def test_sdkconfig_defaults_lock_flash_size_to_16mb(self) -> None:
        self.assertIn("CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y", self.sdkconfig)
        self.assertIn('CONFIG_ESPTOOLPY_FLASHSIZE="16MB"', self.sdkconfig)
        self.assertNotIn("CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y", self.sdkconfig)

    def test_sdkconfig_defaults_select_custom_partition_table(self) -> None:
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", self.sdkconfig)
        self.assertIn(
            f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{PARTITION_FILE}"',
            self.sdkconfig,
        )
        self.assertIn(
            f'CONFIG_PARTITION_TABLE_FILENAME="{PARTITION_FILE}"',
            self.sdkconfig,
        )
        self.assertNotIn("CONFIG_PARTITION_TABLE_SINGLE_APP=y", self.sdkconfig)

    def test_partition_layout_matches_product_contract(self) -> None:
        expected = {
            "nvs": (0x9000, 0x10000),
            "otadata": (0x19000, 0x2000),
            "app0": (0x20000, 0x400000),
            "app1": (0x420000, 0x400000),
            "spiffs": (0x820000, 0x7C0000),
            "coredump": (0xFE0000, 0x20000),
        }
        actual = {
            row["name"]: (row["offset"], row["size"])
            for row in self.partitions()
        }
        self.assertEqual(expected, actual)

    def test_partition_layout_is_non_overlapping_and_fills_16mb(self) -> None:
        rows = sorted(self.partitions(), key=lambda row: int(row["offset"]))
        previous_end = 0x9000
        for row in rows:
            offset = int(row["offset"])
            size = int(row["size"])
            self.assertGreaterEqual(offset, previous_end, row["name"])
            previous_end = offset + size
        self.assertEqual(FLASH_SIZE, previous_end)

    def test_ota_apps_are_64k_aligned(self) -> None:
        apps = [row for row in self.partitions() if row["type"] == "app"]
        self.assertEqual(2, len(apps))
        for app in apps:
            self.assertEqual(0, int(app["offset"]) % 0x10000, app["name"])


if __name__ == "__main__":
    unittest.main()
