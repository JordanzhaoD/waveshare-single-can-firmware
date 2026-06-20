import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
SYNC_FILE = ROOT / "scripts" / "platformio_sync_profile.py"
RUNTIME_FILE = ROOT / "src" / "espidf_runtime.cpp"


class WifiSettingsRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ui = UI_FILE.read_text(encoding="utf-8")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.sync = SYNC_FILE.read_text(encoding="utf-8")
        cls.runtime = RUNTIME_FILE.read_text(encoding="utf-8")

    def assertHasUiId(self, element_id: str) -> None:
        pattern = rf'\bid=(?:"{re.escape(element_id)}"|{re.escape(element_id)}\b)'
        self.assertRegex(self.ui, pattern)

    def test_wifi_ui_has_all_expected_fields(self) -> None:
        required_ids = [
            "wifi-status",
            "wifi-slots",
            "wifi-form",
            "wf-ssid",
            "wf-pass",
            "ap-ssid-input",
            "ap-pass-input",
            "ap-hidden-tgl",
            "gw-nat-tgl",
            "gw-perf-tgl",
            "dns-blacklist",
            "dns-whitelist",
        ]

        for element_id in required_ids:
            with self.subTest(element_id=element_id):
                self.assertHasUiId(element_id)

    def test_wifi_ui_scripts_reference_existing_elements(self) -> None:
        referenced_ids = {
            match.group(1)
            for match in re.finditer(r"\$\('([^']+)'\)", self.ui)
            if match.group(1).startswith("wifi-")
        }
        declared_ids = set()
        for match in re.finditer(r'\bid=(?:"([^"]+)"|([A-Za-z0-9_-]+))', self.ui):
            element_id = match.group(1) or match.group(2)
            if element_id.startswith("wifi-"):
                declared_ids.add(element_id)

        missing = sorted(referenced_ids - declared_ids)
        self.assertEqual([], missing, f"Missing WiFi UI ids: {missing}")

    def test_wifi_backend_exposes_routes_and_preference_keys(self) -> None:
        required_routes = ["/wifi_scan", "/wifi_config", "/wifi_status"]
        required_pref_keys = [
            "wifi_ssid",
            "wifi_pass",
            "wifi_static",
            "wifi_ip",
            "wifi_gw",
            "wifi_mask",
            "wifi_dns",
        ]

        for route in required_routes:
            with self.subTest(route=route):
                self.assertIn(route, self.dash)

        for key in required_pref_keys:
            with self.subTest(pref_key=key):
                self.assertIn(f'"{key}"', self.dash)

    def test_wifi_status_payload_covers_connection_and_config_state(self) -> None:
        expected_fields = [
            '\\"connected\\"',
            '\\"ssid\\"',
            '\\"stored\\"',
            '\\"ip\\"',
            '\\"static\\"',
            '\\"cfg_ip\\"',
            '\\"cfg_gw\\"',
            '\\"cfg_mask\\"',
            '\\"cfg_dns\\"',
            '\\"connecting\\"',
        ]

        status_section = self.dash[self.dash.index("static void handleWifiStatus()") :]
        for field in expected_fields:
            with self.subTest(field=field):
                self.assertIn(field, status_section)

    def test_wifi_settings_backup_roundtrip_fields_exist(self) -> None:
        expected_export_import_fields = [
            '\\"wifi\\":{\\"ssid\\"',
            'doc["wifi"].is<JsonObject>()',
            'doc["wifi"]["ssid"]',
            'doc["wifi"]["pass"]',
            'doc["wifi"]["static"]',
            'doc["wifi"]["ip"]',
            'doc["wifi"]["gw"]',
            'doc["wifi"]["mask"]',
            'doc["wifi"]["dns"]',
        ]

        for field in expected_export_import_fields:
            with self.subTest(field=field):
                self.assertIn(field, self.dash)

    def test_wifi_config_defers_reconnect_until_after_response(self) -> None:
        section = self.dash[
            self.dash.index("static void handleWifiConfig()") :
            self.dash.index("static void handleWifiDelete()")
        ]

        self.assertIn("dashPrepareStaReconnect();", section)
        self.assertIn("dashScheduleSTAConnect(1000);", section)
        self.assertNotIn("dashConnectSTA();", section)
        success_send = 'server.send(200, "application/json", "{\\"ok\\":true,\\"idx\\":"'
        self.assertLess(section.index(success_send), section.index("dashScheduleSTAConnect(1000);"))

    def test_wifi_scan_prepares_sta_mode_before_scan(self) -> None:
        section = self.dash[
            self.dash.index("static void handleWifiScan()") :
            self.dash.index("static void dashPersistWifiSlot")
        ]

        self.assertIn("dashPrepareWifiScan();", section)
        self.assertLess(section.index("dashPrepareWifiScan();"), section.index("WiFi.scanNetworks"))

    def test_espidf_wifi_logging_is_not_info_verbose(self) -> None:
        self.assertIn('esp_log_level_set("wifi", ESP_LOG_WARN);', self.runtime)
        self.assertIn('esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);', self.runtime)

    def test_ap_injection_gate_setting_is_persisted_and_exposed(self) -> None:
        expected_ui_ids = ["ap-gate-tgl"]
        expected_ui_fields = ["saveApGate()", "updateApGateControl(d)"]

        for element_id in expected_ui_ids:
            with self.subTest(ui_id=element_id):
                self.assertHasUiId(element_id)
        expected_backend_fields = [
            "INJECTION_AFTER_AP",
            '"ap_gate"',
            'server.hasArg("apg")',
            '\\"apGate\\"',
            '\\"ia\\"',
            'dashInjectionActive()',
            # Task 5: configurable AP settle delay (0-3000 ms) + auto-restore alias.
            # Regression guard so the prefs key and /config param stay wired even
            # if a later refactor touches the legacy FSD activation gate.
            '"ap_dly"',
            'server.hasArg("ap_delay_ms")',
            'server.hasArg("ap_auto_restore")',
            'dashClampApDelayMs',
        ]

        for field in expected_ui_fields:
            with self.subTest(ui_field=field):
                self.assertIn(field, self.ui)

        for field in expected_backend_fields:
            with self.subTest(backend_field=field):
                self.assertIn(field, self.dash)

        self.assertIn("INJECTION_AFTER_AP", self.sync)

    def test_manual_ota_credentials_can_be_reset_from_dashboard(self) -> None:
        self.assertHasUiId("ota-reset-btn")
        self.assertIn("resetOtaCredentials()", self.ui)
        self.assertRegex(self.ui, r"localStorage\.removeItem\([\"']otaU[\"']\)")
        self.assertRegex(self.ui, r"localStorage\.removeItem\([\"']otaP[\"']\)")


if __name__ == "__main__":
    unittest.main()
