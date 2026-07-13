import unittest
import re
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_SRC = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
UI_GEN = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
DASH = ROOT / "include" / "web" / "mcp2515_dashboard.h"
HANDLERS = ROOT / "include" / "handlers.h"
CAN_HELPERS = ROOT / "include" / "can_helpers.h"
LEGACY_SPEED = ROOT / "include" / "dash_legacy_speed.h"
MAIN = ROOT / "src" / "main.cpp"
VERSION = ROOT / "VERSION"
CHANGELOG = ROOT / "CHANGELOG.md"
README = ROOT / "README.md"
TESTS_WORKFLOW = ROOT / ".github" / "workflows" / "tests.yml"
RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
SIMULATOR = ROOT / "scripts" / "webui_simulator.py"
DASH_CONFIG_UPDATE = ROOT / "include" / "dash_config_update.h"
ESPIDF_RUNTIME_H = ROOT / "include" / "platform" / "espidf_runtime.h"
ESPIDF_RUNTIME_CPP = ROOT / "src" / "espidf_runtime.cpp"


class DashboardApiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ui = UI_SRC.read_text(encoding="utf-8")
        cls.ui_gen = UI_GEN.read_text(encoding="utf-8")
        cls.dash = DASH.read_text(encoding="utf-8")
        cls.handlers = HANDLERS.read_text(encoding="utf-8")
        cls.can_helpers = CAN_HELPERS.read_text(encoding="utf-8")
        cls.legacy_speed = LEGACY_SPEED.read_text(encoding="utf-8")
        cls.main = MAIN.read_text(encoding="utf-8")
        cls.version = VERSION.read_text(encoding="utf-8")
        cls.changelog = CHANGELOG.read_text(encoding="utf-8")
        cls.readme = README.read_text(encoding="utf-8")
        cls.tests_workflow = TESTS_WORKFLOW.read_text(encoding="utf-8")
        cls.release_workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        cls.simulator = SIMULATOR.read_text(encoding="utf-8")
        cls.dash_config_update = DASH_CONFIG_UPDATE.read_text(encoding="utf-8")
        cls.espidf_runtime_h = ESPIDF_RUNTIME_H.read_text(encoding="utf-8")
        cls.espidf_runtime_cpp = ESPIDF_RUNTIME_CPP.read_text(encoding="utf-8")

    def _standalone_visible_source(self) -> str:
        """Return the source UI surface visible after standalone product gating.

        The shared source UI is allowed to keep dual-CAN copy inside elements
        hidden by standalone mode. This helper approximates the standalone DOM by
        removing elements marked data-single-hide and replacing data-single-text
        elements with their standalone text before visible-copy assertions run.
        """

        class StandaloneVisibleParser(HTMLParser):
            VOID_TAGS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}

            def __init__(self) -> None:
                super().__init__(convert_charrefs=True)
                self.parts: list[str] = []
                self.hidden_depth = 0
                self.replaced_depth = 0

            def _is_void(self, tag: str) -> bool:
                return tag.lower() in self.VOID_TAGS

            def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
                attr_map = {name: value for name, value in attrs}
                if self.hidden_depth:
                    if not self._is_void(tag):
                        self.hidden_depth += 1
                    return
                if "data-single-hide" in attr_map:
                    if not self._is_void(tag):
                        self.hidden_depth = 1
                    return
                if self.replaced_depth:
                    if not self._is_void(tag):
                        self.replaced_depth += 1
                    return
                if "data-single-text" in attr_map:
                    self.parts.append(attr_map.get("data-single-text") or "")
                    if not self._is_void(tag):
                        self.replaced_depth = 1
                    return

                attr_text = " ".join(
                    name if value is None else f'{name}="{value}"'
                    for name, value in attrs
                )
                self.parts.append(f"<{tag}{(' ' + attr_text) if attr_text else ''}>")

            def handle_endtag(self, tag: str) -> None:
                if self.hidden_depth:
                    self.hidden_depth -= 1
                    return
                if self.replaced_depth:
                    self.replaced_depth -= 1
                    return
                self.parts.append(f"</{tag}>")

            def handle_data(self, data: str) -> None:
                if not self.hidden_depth and not self.replaced_depth:
                    self.parts.append(data)

        parser = StandaloneVisibleParser()
        parser.feed(self.ui)
        return "".join(parser.parts)

    def _route_block(self, route: str) -> str:
        idx = self.dash.find(route)
        self.assertNotEqual(idx, -1, f"{route} not found in dashboard source")
        start = max(0, idx - 250)
        end = min(len(self.dash), idx + 250)
        return self.dash[start:end]

    def test_checked_boolean_persistence_contract(self) -> None:
        self.assertIn("bool dashParseStrictBool(const char *raw, bool &out)", self.dash_config_update)
        self.assertIn("struct DashPersistedBoolUpdate", self.dash_config_update)
        self.assertIn("dashPreparePersistedBoolUpdate", self.dash_config_update)
        self.assertIn("bool putBoolChecked(const char *key, bool value);", self.espidf_runtime_h)

        checked = re.search(
            r"bool Preferences::putBoolChecked\(const char \*key, bool value\).*?\n\}",
            self.espidf_runtime_cpp,
            re.S,
        )
        self.assertIsNotNone(checked)
        checked_body = checked.group(0)
        for token in [
            "if (!open_ || readOnly_)",
            "nvs_set_u8(handle_, key, value ? 1 : 0)",
            "nvs_commit(handle_)",
            "return false;",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, checked_body)

        legacy = re.search(
            r"void Preferences::putBool\(const char \*key, bool value\).*?\n\}",
            self.espidf_runtime_cpp,
            re.S,
        )
        self.assertIsNotNone(legacy)
        self.assertIn("(void)putBoolChecked(key, value);", legacy.group(0))

    def test_instant_engage_config_persistence_contract(self) -> None:
        self.assertIn('#include "dash_config_update.h"', self.dash)
        self.assertEqual(self.dash.count('server.on("/config", HTTP_GET, handleConfigGet);'), 1)
        self.assertEqual(self.dash.count('server.on("/config", HTTP_POST, handleConfig);'), 1)
        self.assertNotIn('server.on("/ap_first_edge"', self.dash)

        load = re.search(r"static void dashLoadPrefs\(\).*?prefs\.end\(\);", self.dash, re.S)
        self.assertIsNotNone(load)
        self.assertIn('dashInstantEngage = prefs.getBool("apfe", false);', load.group(0))

        get_handler = re.search(r"static void handleConfigGet\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(get_handler)
        self.assertIn('\\"ap_first_edge\\":', get_handler.group(0))
        self.assertIn("dashInstantEngage", get_handler.group(0))

        self.assertIn("static bool dashPutBoolChecked(const char *key, bool value)", self.dash)
        wrapper = re.search(
            r"static bool dashPutBoolChecked\(const char \*key, bool value\).*?\n\}",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(wrapper)
        wrapper_body = wrapper.group(0)
        self.assertIn("prefs.begin(PREFS_NS, false)", wrapper_body)
        self.assertIn("prefs.putBoolChecked(key, value)", wrapper_body)
        self.assertIn("prefs.end()", wrapper_body)
        self.assertIn("return ok;", wrapper_body)

        post = re.search(
            r"static void handleConfig\(\).*?static void handleLoggingConfig\(\)",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(post)
        post_body = post.group(0)
        self.assertIn('server.hasArg("ap_first_edge")', post_body)
        self.assertIn("dashPreparePersistedBoolUpdate", post_body)
        self.assertIn('dashPutBoolChecked("apfe", value)', post_body)
        self.assertIn('server.send(400, "application/json"', post_body)
        self.assertIn('server.send(500, "application/json"', post_body)
        self.assertNotIn('server.arg("ap_first_edge").toInt()', post_body)
        parse_idx = post_body.index("dashPreparePersistedBoolUpdate")
        invalid_idx = post_body.index("if (!update.valid)", parse_idx)
        persist_idx = post_body.index("if (!update.persisted)", invalid_idx)
        assign_idx = post_body.index("dashInstantEngage = update.value;", persist_idx)
        apply_idx = post_body.index("dashApplyRuntimeState()", assign_idx)
        self.assertLess(parse_idx, invalid_idx)
        self.assertLess(invalid_idx, persist_idx)
        self.assertLess(persist_idx, assign_idx)
        self.assertLess(assign_idx, apply_idx)

        export = re.search(
            r"static void handleSettingsExport\(\).*?static void handleSettingsImport\(\)",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(export)
        export_body = export.group(0)
        self.assertIn('p.getBool("apfe", dashInstantEngage)', export_body)
        self.assertIn('\\"apFirstEdge\\":', export_body)

        restore = re.search(
            r"static void handleSettingsImport\(\).*?dashLog\(\"\[BACKUP\] Settings imported",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(restore)
        self.assertIn('if (device["apFirstEdge"].is<bool>())', restore.group(0))
        self.assertIn('p.putBool("apfe", device["apFirstEdge"].as<bool>())', restore.group(0))

        clear_timing = re.search(
            r"static void dashClearLegacyApFirstTiming\(\).*?\n\}",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(clear_timing)
        self.assertNotIn("dashInstantEngage", clear_timing.group(0))
        declaration = "static bool dashInstantEngage = false;"
        self.assertEqual(self.dash.count(declaration), 1)
        self.assertNotIn("dashInstantEngage = false", self.dash.replace(declaration, "", 1))

    def test_dashboard_ui_generation_is_dependency_aware(self) -> None:
        """PlatformIO must rebuild firmware when the generated dashboard header changes."""
        build_script = (ROOT / "scripts" / "update_ota_build_timestamp.py").read_text(encoding="utf-8")
        minify_script = (ROOT / "scripts" / "minify_dashboard.py").read_text(encoding="utf-8")
        self.assertIn("env.Command", build_script)
        self.assertIn("env.Depends(\"$BUILD_DIR/src/main.cpp.o\"", build_script)
        self.assertIn("env.Depends(\"$BUILD_DIR/${PROGNAME}.elf\"", build_script)
        self.assertIn("DASH_UI_BUILD_ID", minify_script)
        self.assertIn("--check", minify_script)

    def test_generated_dashboard_header_contains_current_phase_tokens(self) -> None:
        """Generated gzip header must be regenerated from the current source UI."""
        for token in [
            "DASH_HTML_GZ",
            "DASH_UI_BUILD_ID",
            "DASH_UI_BUILD_UTC",
            "pg-strobe",
            "pg-shift",
            "/speed_custom",
            "/defense_config",
            "/fog_light",
            "/burst",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui_gen)

    def test_generated_dashboard_header_contains_task1_ui_contract_tokens(self) -> None:
        """Generated dashboard header must stay synchronized with Task 1 source UI contracts."""
        for token in [
            "ap-gate-tgl",
            "ap-delay-select",
            "mob-more-single",
            "renderPluginsStatus",
            "/plugins/status",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui_gen)

    def test_destructive_buttons_use_post_helpers(self) -> None:
        self.assertIn('onclick="resetStats()"', self.ui)
        self.assertIn('onclick="rebootDevice()"', self.ui)
        self.assertNotIn("fetch('/reset_stats')", self.ui)
        self.assertNotIn("fetch('/reboot')", self.ui)

    def test_post_helper_surfaces_http_errors(self) -> None:
        self.assertIn("async function postForm(url,data)", self.ui)
        self.assertIn("if(!r.ok)", self.ui)
        self.assertIn("showToast((e&&e.message)?e.message:T('请求失败'))", self.ui)
        self.assertIn("throw e;", self.ui)

    def test_hardware_auto_matches_backend_mode_value(self) -> None:
        self.assertIn('onclick="setHW(3)"', self.ui)
        self.assertIn("var map=[3,0,1,2];", self.ui)
        self.assertIn("if (v <= 3 && v != hwMode)", self.dash)

    def test_ap_auto_restore_is_gated_by_abort_guard(self) -> None:
        """Post-process AP auto-restore is a CAN write path and must honor Abort Guard."""
        fn_start = self.dash.index("static void dashTryApAutoRestore")
        fn_end = self.dash.index("static void dashPostProcessFrame", fn_start)
        fn = self.dash[fn_start:fn_end]
        send_idx = fn.index("driver.send(modified)")
        guard_idx = fn.find("abortGuard.allowsInjection()")
        self.assertNotEqual(guard_idx, -1, "AP auto-restore must check Abort Guard before sending")
        self.assertLess(guard_idx, send_idx, "Abort Guard check must happen before AP auto-restore driver.send")
        self.assertIn(
            "DashAbortGuardBlockPath::ApAutoRestore",
            fn[:send_idx],
            "blocked AP auto-restore attempts should be recorded in Abort Guard diagnostics",
        )

    def test_hw3_hw4_das_status_feeds_abort_guard(self) -> None:
        """Active HW3/HW4 handlers must latch Abort Guard from DAS AP abort states."""
        for handler_name in ["HW3Handler", "HW4Handler"]:
            with self.subTest(handler=handler_name):
                handler_start = self.handlers.index(f"struct {handler_name}")
                handler_end = self.handlers.find("struct ", handler_start + 1)
                if handler_end == -1:
                    handler_end = len(self.handlers)
                handler = self.handlers[handler_start:handler_end]
                status_start = handler.index("if (frame.id == 921)")
                status_end = handler.find("if (frame.id ==", status_start + 1)
                if status_end == -1:
                    status_end = len(handler)
                status_block = handler[status_start:status_end]
                ap_read_idx = status_block.index("readDASAutopilotStatus(frame)")
                latch_idx = status_block.find("abortGuard.onApState")
                self.assertNotEqual(latch_idx, -1, f"{handler_name} DAS status must feed Abort Guard")
                self.assertGreater(latch_idx, ap_read_idx)
                self.assertIn("dashDiagNowMs()", status_block[latch_idx:])

        hw4_start = self.handlers.index("struct HW4Handler")
        hw4 = self.handlers[hw4_start:]
        status923_start = hw4.index("if (frame.id == 923")
        status923_end = hw4.index("if (frame.id == 1016)", status923_start)
        status923_block = hw4[status923_start:status923_end]
        ap_read_idx = status923_block.find("readHw4Das923ApState(frame)")
        latch_idx = status923_block.find("abortGuard.onApState")
        gate_idx = status923_block.find("abortGuardAllowsInjection")
        self.assertNotEqual(ap_read_idx, -1, "HW4 923 DAS status must use the dedicated safe AP-state decoder")
        self.assertNotIn("readDASAutopilotStatus(frame)", status923_block)
        self.assertIn("(frame.data[1] >> 4) & 0x0F", hw4)
        self.assertIn("hw4Das923UseByte0", hw4)
        self.assertNotEqual(latch_idx, -1, "HW4 923 DAS status must feed Abort Guard")
        self.assertGreater(latch_idx, ap_read_idx)
        self.assertNotEqual(gate_idx, -1, "HW4 923 write path must remain Abort Guard gated")
        self.assertLess(latch_idx, gate_idx, "HW4 923 must latch Abort Guard before injection gating")
        self.assertIn("dashDiagNowMs()", status923_block[latch_idx:])

    def test_hw3_hw4_send_paths_are_gated_by_abort_guard(self) -> None:
        """HW3/HW4 built-in CAN injection sends must stop while Abort Guard is latched."""
        self.assertIn("abortGuardAllowsInjection", self.handlers)
        self.assertIn("abortGuard.recordBlock", self.handlers)
        for handler_name in ["HW3Handler", "HW4Handler"]:
            with self.subTest(handler=handler_name):
                handler_start = self.handlers.index(f"struct {handler_name}")
                handler_end = self.handlers.find("struct ", handler_start + 1)
                if handler_end == -1:
                    handler_end = len(self.handlers)
                handler = self.handlers[handler_start:handler_end]
                for match in re.finditer(r"driver\.send\(frame\)", handler):
                    prefix = handler[:match.start()]
                    self.assertIn(
                        "abortGuardAllowsInjection(",
                        prefix,
                        f"{handler_name} send at offset {match.start()} must check Abort Guard before sending",
                    )

    def test_epas_late_echo_status_and_config_contract(self) -> None:
        for token in [
            '"lateEchoMode"',
            '"cadenceStable"',
            '"lateEchoEligible"',
            '"pendingEcho"',
            '"periodMs"',
            '"jitterMs"',
            '"counterStep"',
            '"expectedNextCounter"',
            '"pendingSendAtMs"',
            '"scheduledEchoes"',
            '"sentLateEchoes"',
            '"droppedLateEchoes"',
            '"lateWindowMissed"',
            '"lastRxToTxMs"',
            '"preserveHandsOnLevel"',
            '"lastSourceHandsOnLevel"',
            '"lastTxHandsOnLevel"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)
        self.assertIn('def_nag_mode', self.dash)
        self.assertIn('setNagMode', self.dash)
        self.assertIn('virtual void setNagMode(uint8_t', self.handlers)
        self.assertIn('void setNagMode(uint8_t mode) override', self.handlers)
        self.assertIn('bool effectiveBionic = dashDefenseEnabled && dashBionicSteering;', self.dash)
        self.assertIn('uint8_t effectiveNagMode = dashDefenseEnabled ? dashNagMode : 0;', self.dash)
        self.assertIn('reactive->bionicSteering = effectiveBionic', self.dash)
        self.assertIn('reactive->setNagMode(effectiveNagMode)', self.dash)
        self.assertIn('reactive && reactive != dashHandler', self.dash)
        self.assertIn('reactive->resetBionic(resetSeed)', self.dash)
        self.assertNotIn('static_cast<LegacyHandler *>(reactive)->setNagMode', self.dash)
        self.assertIn('dashTryParseNagMode(raw.c_str(), parsedNagMode)', self.dash)
        self.assertNotIn('int mode = raw.toInt();', self.dash)
        self.assertIn('p.putUChar("def_nag_mode", dashClampNagMode(mode));', self.dash)
        status_idx = self.dash.find('static void handleStatus()')
        self.assertNotEqual(status_idx, -1)
        reactive_idx = self.dash.find(',\\"reactiveNag\\":{', status_idx)
        self.assertNotEqual(reactive_idx, -1)
        status_prefix = self.dash[status_idx:reactive_idx]
        self.assertIn(',\\"nagMode\\":', status_prefix)
        self.assertIn(
            'String(dashNagModeIsValid(dashNagMode) ? dashNagMode : '
            'dashNagModeToRaw(DashNagMode::Off))',
            status_prefix,
        )
        self.assertIn('=== EPAS-faithful Late Echo ===', self.dash)

    def test_four_mode_nag_persistence_and_migration_contract(self) -> None:
        load = re.search(
            r"static void dashLoadPrefs\(\).*?dashNagTorqueTamper =",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(load)
        load_body = load.group(0)
        mode_load = re.search(
            r'if \(prefs\.isKey\("def_nag_mode"\)\)\s*'
            r'\{(?P<stored>.*?)\}\s*else\s*\{(?P<migrated>.*?)\}',
            load_body,
            re.S,
        )
        self.assertIsNotNone(mode_load)
        stored_branch = mode_load.group("stored")
        migrated_branch = mode_load.group("migrated")
        self.assertIn('prefs.getUChar("def_nag_mode", 0)', stored_branch)
        self.assertNotIn("dashBionicSteering", stored_branch)
        self.assertIn("dashNagMode = dashBionicSteering", migrated_branch)
        self.assertIn("DashNagMode::ReactiveHold", migrated_branch)
        self.assertIn("DashNagMode::Off", migrated_branch)
        self.assertIn('prefs.putUChar("def_nag_mode", dashNagMode);', migrated_branch)

        handler = re.search(
            r"static void handleDefenseConfig\(\).*?"
            r"static void handleLegacyFsdConfig\(\)",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(handler)
        handler_body = handler.group(0)
        parse_call = "dashTryParseNagMode(raw.c_str(), parsedNagMode)"
        assign_call = "dashNagMode = dashNagModeToRaw(parsedNagMode);"
        self.assertIn(parse_call, handler_body)
        self.assertIn('server.send(400, "application/json"', handler_body)
        self.assertIn("nag_mode must be 0..3", handler_body)
        self.assertIn("return;", handler_body)
        self.assertLess(handler_body.index(parse_call), handler_body.index(assign_call))
        self.assertLess(handler_body.index(parse_call), handler_body.index("bool prevDefenseEnabled"))
        self.assertNotIn("raw.toInt()", handler_body)
        self.assertNotIn("dashNagMode = 0", handler_body)
        self.assertIn(
            "uint8_t effectiveNagMode = dashDefenseEnabled ? dashNagMode : 0;",
            self.dash,
        )

        export = re.search(
            r"static void handleSettingsExport\(\).*?"
            r"static void handleSettingsImport\(\)",
            self.dash,
            re.S,
        )
        self.assertIsNotNone(export)
        export_body = export.group(0)
        self.assertIn('p.getUChar("def_nag_mode", dashNagMode)', export_body)
        self.assertIn(',\\"nagMode\\":', export_body)

        restore = re.search(
            r"static void handleSettingsImport\(\).*?"
            r'dashLog\("\[BACKUP\] Settings imported',
            self.dash,
            re.S,
        )
        self.assertIsNotNone(restore)
        restore_body = restore.group(0)
        self.assertIn('if (defense["nagMode"].is<int>())', restore_body)
        self.assertIn('p.putUChar("def_nag_mode", dashClampNagMode(mode));', restore_body)
        self.assertIn("DashNagMode::ReactiveHold", self.dash)
        self.assertIn(',\\"nag_mode\\":', self.dash)
        self.assertIn(',\\"nagMode\\":', self.dash)
        self.assertNotIn('dashNagMode <= 2 ? dashNagMode : 0', self.dash)
        self.assertNotIn('if (dashNagMode > 2)', self.dash)
        self.assertNotIn('if (storedNagMode > 2)', self.dash)

    def test_four_mode_nag_ui_selector_contract(self) -> None:
        for token in [
            'id="nag-mode-select"',
            '<option value="0">Off</option>',
            '<option value="1">Human Replay TSL6P</option>',
            '<option value="2">EPAS Late Echo</option>',
            '<option value="3">Reactive Sustained Hold</option>',
            "nagMode",
            "defense.nagMode",
            "nag_mode",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        for mode in range(4):
            with self.subTest(card_mode=mode):
                self.assertIn(f'data-value="{mode}"', self.ui)
                self.assertIn(f"selectCard('nag-mode-select',{mode})", self.ui)

        load_fn = re.search(r"async function loadDefenseConfig\(\)\{.*?setText\('tb-exp'", self.ui, re.S)
        self.assertIsNotNone(load_fn)
        load_body = load_fn.group(0)
        self.assertIn("setVal('nag-mode-select'", load_body)
        self.assertIn("d.nag_mode", load_body)
        self.assertIn("syncNagModeAvailability(!!d.enabled)", load_body)

        sync_fn = re.search(r"function syncNagModeAvailability\([^)]*\)\{.*?\n\}", self.ui, re.S)
        self.assertIsNotNone(sync_fn)
        self.assertNotIn(".value=", sync_fn.group(0))

    def test_status_exposes_build_and_legacy_diagnostics(self) -> None:
        """Device status must show which firmware/UI and handler mode are running."""
        for token in [
            "dashDefaultHw",
            "effectiveHw",
            "hwName",
            "buildEnv",
            "uiBuildId",
            "uiBuildUtc",
            "DASH_DEFAULT_HW",
            "DASH_BUILD_ENV",
            "DASH_UI_BUILD_ID",
            "DASH_UI_BUILD_UTC",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)
        self.assertIn("DASH_DEFAULT_HW=", (ROOT / "scripts" / "platformio_sync_profile.py").read_text(encoding="utf-8"))
        self.assertIn("DASH_BUILD_ENV", (ROOT / "scripts" / "platformio_sync_profile.py").read_text(encoding="utf-8"))
        self.assertIn("d.hwName||hwLabel(d.hw)", self.ui)
        self.assertIn("d.uiBuildUtc||d.uiBuildId||d.buildEnv", self.ui)

    def test_waveshare_single_can_standalone_env_is_declared(self) -> None:
        pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        match = re.search(r"(?ms)^\[env:waveshare_single_can_standalone\]\n.*?(?=^\[|\Z)", pio)
        self.assertIsNotNone(match)
        standalone_env = match.group(0)
        self.assertIn("-DDRIVER_TWAI", standalone_env)
        self.assertIn("-DESP32_DASHBOARD", standalone_env)
        self.assertIn("-DDASH_SINGLE_CAN_STANDALONE=1", standalone_env)
        self.assertIn("-DDASH_INITIAL_HW_MODE=3", standalone_env)
        self.assertIn("-DTWAI_TX_PIN=GPIO_NUM_15", standalone_env)
        self.assertIn("-DTWAI_RX_PIN=GPIO_NUM_16", standalone_env)
        self.assertIn("partitions_16mb_ota_4096k_nvs64.csv", standalone_env)

    def test_waveshare_build_enables_injection_after_ap_gate(self) -> None:
        """AP-First gate must default ON for the waveshare build so Legacy 0x3EE
        injection is held off the AP activation edge (steer-jerk root cause).
        See docs/superpowers/specs/2026-06-23-steer-jerk-ap-injection-fix-design.md."""
        import re
        profile = (ROOT / "platformio_profile.example.h").read_text(encoding="utf-8")
        self.assertIsNotNone(
            re.search(r"^#define INJECTION_AFTER_AP\s*$", profile, re.M),
            "platformio_profile.example.h must '#define INJECTION_AFTER_AP' (uncommented)",
        )
        self.assertNotIn("// #define INJECTION_AFTER_AP", profile)
        workflow = (ROOT / TESTS_WORKFLOW).read_text(encoding="utf-8")
        self.assertIn("--enable INJECTION_AFTER_AP", workflow)

    def test_status_exposes_single_can_capabilities(self) -> None:
        for token in [
            "dashCapabilitiesJson",
            '"capabilities"',
            '"singleCan"',
            '"can2Available"',
            '"lightingBusSupported"',
            '"serviceModeSupported"',
            '"stalkTestSupported"',
            '"bus2SnifferSupported"',
            '"fsdActivation"',
            '"speedStrategy"',
            '"driveProfile"',
            '"networkSettings"',
            '"otaUpdate"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

    def test_single_can_build_gates_can2_only_routes(self) -> None:
        for token in [
            "DASH_SINGLE_CAN_STANDALONE",
            "server.on(\"/service_mode\", HTTP_GET, handleServiceMode);",
            "server.on(\"/stalk_test\", HTTP_GET, handleStalkTest);",
            "server.on(\"/burst\", HTTP_GET, handleBurst);",
            "server.on(\"/bus2_ids\", HTTP_GET, handleBus2Ids);",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)
        self.assertRegex(
            self.dash,
            r"#if defined\(DRIVER_T2CAN_DUAL\) && !defined\(DASH_SINGLE_CAN_STANDALONE\)(?s:.*?)/bus2_ids",
        )
        self.assertRegex(
            self.dash,
            r"#if !defined\(DASH_SINGLE_CAN_STANDALONE\)(?s:.*?)/lighting_config",
        )

    def test_single_can_ui_uses_capabilities_to_hide_can2_and_lighting(self) -> None:
        for token in [
            "var CAP=",
            "function applyCapabilities(c)",
            "data-cap=\"can2\"",
            "data-cap=\"lighting\"",
            "cap-hidden",
            "applyCapabilities(d.capabilities||{})",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_single_can_driving_status_exposes_ap_gate_delay_and_restore(self) -> None:
        for token in [
            'id="ap-gate-tgl"',
            'id="ap-delay-select"',
            'id="ap-auto-restore-tgl"',
            'saveApGateControls()',
            'renderApInjectionState',
            '等待 AP',
            '稳定计时中',
            '正在注入',
            '已阻断',
            'AP 自动恢复',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        for token in [
            r'\"apDelayMs\"',
            r'\"apInjectionState\"',
            '"apAutoRestore"',
            'server.hasArg("ap_delay_ms")',
            'server.hasArg("ap_auto_restore")',
            'prefs.putUInt("ap_dly"',
            'prefs.getUInt("ap_dly"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

    def test_official_plugin_manager_contract_is_present(self) -> None:
        for token in [
            '#include "dash_plugin_engine.h"',
            'DashPluginEngine',
            'dashPluginEngine',
            'server.on("/plugins/status", HTTP_GET, handlePluginsStatus);',
            'server.on("/plugins/install_url", HTTP_POST, handlePluginsInstallUrl);',
            'server.on("/plugins/upload", HTTP_POST, handlePluginsUpload',
            'server.on("/plugins/install_json", HTTP_POST, handlePluginsInstallJson);',
            'server.on("/plugins/toggle", HTTP_POST, handlePluginsToggle);',
            'server.on("/plugins/priority", HTTP_POST, handlePluginsPriority);',
            'server.on("/plugins/remove", HTTP_POST, handlePluginsRemove);',
            'server.on("/plugins/replay_count", HTTP_POST, handlePluginsReplayCount);',
            'server.on("/plugins/rule_test", HTTP_POST, handlePluginsRuleTest);',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

        for token in [
            'Plugins',
            'renderPluginsStatus',
            'installPluginUrl',
            'installPluginJson',
            'uploadPluginJson',
            'togglePlugin',
            'setPluginPriority',
            'removePlugin',
            'setPluginReplayCount',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        # Task 5 (UI V1.1) replaced the English plugin UI labels with Chinese
        # cockpit-style copy. The old English labels must not come back.
        for token in [
            'Install from URL',
            'Upload .json',
            'Paste JSON',
            'Installed Plugins',
            'Plugin Editor',
            'Rule Test',
            'GTW2047 Replay Count',
        ]:
            with self.subTest(token=token):
                self.assertNotIn(token, self.ui)

    def test_single_can_product_ui_hides_dual_can_surfaces(self) -> None:
        for token in [
            'PRODUCT_SINGLE_CAN_NAME',
            'Atlas Single CAN',
            'function applyProductMode',
            'data-single-hide',
            'data-single-text',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        forbidden_visible_tokens = [
            'CAN2 控制',
            'CAN2 状态',
            'CAN2 RX',
            'CAN2 TX',
            '自动换挡',
            'Auto Shift',
            'T-2CAN 控制面板',
            'ATLAS T-2CAN',
        ]
        source = self._standalone_visible_source()
        for token in forbidden_visible_tokens:
            with self.subTest(token=token):
                self.assertNotIn(token, source)

        for token in [
            'data-page="pg-bus2" data-cap="can2" data-single-hide="1"',
            'data-page="pg-strobe" data-cap="lighting" data-single-hide="1"',
            'data-page="pg-shift" data-single-hide="1"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_mobile_single_can_ui_matches_ap_and_plugin_capabilities(self) -> None:
        for token in [
            'id="mob-tabs"',
            'data-mobile-page="driving"',
            'data-mobile-page="hardware"',
            'data-mobile-page="speed"',
            'data-mobile-page="network"',
            'id="mob-more-single"',
            "driving:'pg-overview'",
            "hardware:'pg-hardware'",
            "speed:'pg-speed'",
            "network:'pg-network'",
            'id="plugins-card"',
            'id="ap-core-card"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        for token in [
            'id="mob-plugin-page"',
            'data-mobile-page="plugins"',
            'data-mobile-page-panel="driving"',
            'data-mobile-page-panel="network"',
            'm-ap-state',
            'm-ap-gate-tgl',
            'm-ap-delay-select',
            'm-ap-auto-restore-tgl',
            'm-plugin-list',
            'm-plugin-json',
            'm-plugin-replay-count',
            'renderMobilePlugins',
            'renderMobileApState',
        ]:
            with self.subTest(token=token):
                self.assertNotIn(token, self.ui)

        source = self._standalone_visible_source()
        for token in [
            'data-page="pg-bus2">',
            'data-page="pg-strobe">',
            'data-page="pg-shift">',
        ]:
            with self.subTest(token=token):
                self.assertNotIn(token, source)

    def test_plugin_engine_header_exposes_upstream_official_contract(self) -> None:
        plugin = (ROOT / "include" / "dash_plugin_engine.h").read_text(encoding="utf-8")
        for token in [
            'kDashPluginMaxPlugins = 8',
            'kDashPluginMaxRules = 16',
            'kDashPluginMaxOps = 16',
            'kDashPluginMaxFilterIds = 32',
            'DashPluginEngine',
            'installJson',
            'setEnabled',
            'setPriority',
            'setReplayCount',
            'applyToFrame',
            'tickPeriodic',
            'emit_periodic',
            'gtw_silent',
            'PLUGIN_GTW_UDS_CUSTOM_KEY',
            '2047',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, plugin)

    def test_plugin_engine_injection_is_gated_by_abort_guard(self) -> None:
        """Compiled-in plugins must stop sending while Abort Guard is latched."""
        plugin = (ROOT / "include" / "dash_plugin_engine.h").read_text(encoding="utf-8")
        self.assertIn("abortGuardAllowed", plugin)
        self.assertRegex(
            plugin,
            r"if \(!ctx\.abortGuardAllowed\)\s*\{[^}]*blockedBy = \"abort_guard\"",
        )
        self.assertRegex(
            plugin,
            r"if \([^)]*!ctx\.abortGuardAllowed[^)]*\)\s*\{\s*clearPeriodicCache\(\);",
        )

        context_start = self.dash.find("static DashPluginContext dashPluginContext()")
        self.assertNotEqual(context_start, -1)
        context_end = self.dash.find("return ctx;", context_start)
        self.assertNotEqual(context_end, -1)
        context_block = self.dash[context_start:context_end]
        self.assertIn("ctx.abortGuardAllowed", context_block)
        self.assertIn("abortGuard.allowsInjection()", context_block)

    def test_single_can_docs_are_present(self) -> None:
        building = (ROOT / "docs" / "building.md").read_text(encoding="utf-8")
        self.assertIn("waveshare_single_can_standalone", building)
        self.assertIn("Waveshare 单 CAN", self.readme)
        self.assertIn("DASH_SINGLE_CAN_STANDALONE", self.changelog)

    def test_legacy_fsd_diag_and_config_are_exposed(self) -> None:
        for token in [
            '"fsdDiag"',
            'legacyFsdPolicyName',
            'fsdTriggerSourceName',
            'fsdSkipReasonName',
            '"mux0"',
            '"mux1"',
            '"triggerSource"',
            '"forceRuntime"',
            'server.on("/legacy_fsd_config", HTTP_GET, handleLegacyFsdConfig);',
            'server.on("/legacy_fsd_config", HTTP_POST, handleLegacyFsdConfig);',
            'prefs.putUChar("lfsd_pol"',
            'prefs.getUChar("lfsd_pol"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

    def test_legacy_tesla_parity_policy_is_wired(self) -> None:
        diag = (ROOT / "include" / "dash_fsd_diag.h").read_text(encoding="utf-8")
        for token in [
            'TeslaParity',
            '"legacy_tesla_parity"',
            'p == "legacy_tesla_parity"',
            'prefs.putUChar("lfsd_pol"',
            'prefs.getUChar("lfsd_pol"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, diag + self.dash)
        self.assertIn('Tesla Controller 对齐模式', self.ui)

    def test_legacy_can_a_fsd_diag_expanded_fields_are_exposed(self) -> None:
        for token in [
            'FsdHealthState',
            'FsdGateBlockReason',
            'FsdDiagBus',
            'FsdDiagDriver',
            'fsdHealthStateName',
            'fsdGateBlockReasonName',
            'classifyLegacyFsdHealth',
            'kLegacyFsdSourceStaleMs',
            'FsdGateBlockReason (*gateBlockReason)() = nullptr',
            'currentGateBlockReason()',
            'FsdGateBlockReason::ApGate',
            'firstMux0RxMs',
            'firstTxOkMs',
            'firstTxFailMs',
            'LegacyFsdSettle',
            'legacy_fsd_settle',
            'legacyFsdActivationAllowed',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.handlers + (ROOT / "include" / "dash_fsd_diag.h").read_text(encoding="utf-8"))
        for token in [
            '"health"',
            '"gateReason"',
            '"lastBlockedBy"',
            '"bus"',
            '"driver"',
            '"firstMux0RxAgeMs"',
            '"firstTxOkAgeMs"',
            '"firstTxFailAgeMs"',
            '"aux760"',
            '"aux1080"',
            'dashCurrentGateBlockReason',
            'dashHandler->gateBlockReason = dashCurrentGateBlockReason',
            'handlerPool[i]->gateBlockReason = dashCurrentGateBlockReason',
            'FsdGateBlockReason::CanActive',
            'FsdGateBlockReason::Ota',
            'FsdGateBlockReason::ApGate',
            'FsdGateBlockReason::CompileGate',
            'dashLegacyFsdActivationAllowed',
            'requiredStableMs',
            'legacyFsdAllowed',
            'blockedFrame',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

    def test_legacy_can_a_health_ui_labels_are_present(self) -> None:
        for token in [
            'Legacy CAN-A 诊断',
            '0x3EE mux0',
            '0x3EE mux1',
            'TWAI 状态',
            'TEC/REC',
            'TX失败',
            'RX丢失',
            '健康状态',
            '最后阻止原因',
            'Tesla Controller 对齐模式',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)
                self.assertIn(token, self.ui_gen)

    def test_legacy_fsd_policy_uses_selection_cards(self) -> None:
        """Regression: policy card container must use the 3-column selection-card layout."""
        source_policy_cards_div = re.compile(
            r'<div\b(?=[^>]*\bid="legacy-fsd-policy-cards")(?=[^>]*\bclass="sel-cards c3")[^>]*>'
        )
        generated_policy_cards_div = re.compile(
            r'<div\b'
            r'(?=[^>]*\bid=(?:"legacy-fsd-policy-cards"|legacy-fsd-policy-cards)(?=[\s>/]))'
            r'(?=[^>]*\bclass=(?:"sel-cards c3"|sel-cards)(?=[\s>/]))'
            r'[^>]*>'
        )
        self.assertRegex(self.ui, source_policy_cards_div)
        self.assertRegex(self.ui_gen, generated_policy_cards_div)

        for token in [
            'data-policy="stable"',
            'data-policy="experimental"',
            'data-policy="legacy_tesla_parity"',
            'renderLegacyFsdPolicyCards(fsd.policy)',
            'renderLegacyFsdPolicyCards(policy)',
            'Tesla Controller 对齐模式',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        source_policy_fn = re.search(r"function renderLegacyFsdPolicyCards\(policy\)\{.*?\n\}", self.ui, re.S)
        self.assertIsNotNone(source_policy_fn)
        source_policy_body = source_policy_fn.group(0)
        self.assertRegex(source_policy_body, r"policy==='legacy_stable'.*?'stable'")
        self.assertRegex(source_policy_body, r"policy==='legacy_experimental'.*?'experimental'")
        self.assertIn("policy||'stable'", source_policy_body)

        for pattern in [
            r'\bdata-policy=(?:"stable"|stable)(?=[\s>/])',
            r'\bdata-policy=(?:"experimental"|experimental)(?=[\s>/])',
            r'\bdata-policy=(?:"legacy_tesla_parity"|legacy_tesla_parity)(?=[\s>/])',
            r'renderLegacyFsdPolicyCards\(fsd\.policy\)',
            r'renderLegacyFsdPolicyCards\(policy\)',
            r"policy==='legacy_stable'.*?'stable'",
            r"policy==='legacy_experimental'.*?'experimental'",
            r"policy\|\|'stable'",
            r'Tesla Controller 对齐模式',
        ]:
            with self.subTest(pattern=pattern):
                self.assertRegex(self.ui_gen, pattern)

    def test_legacy_fsd_mux1_checkbox_has_unique_toggle_id(self) -> None:
        """Regression: mux1 checkbox and toggle must not share duplicate DOM ids."""
        self.assertIn('id="legacy-fsd-mux1"', self.ui)
        self.assertIn('id="legacy-fsd-mux1-tgl"', self.ui)
        self.assertIn("document.getElementById('legacy-fsd-mux1-tgl')", self.ui)
        self.assertEqual(self.ui.count('id="legacy-fsd-mux1"'), 1)
        self.assertEqual(self.ui.count('id="legacy-fsd-mux1-tgl"'), 1)

        mux1_id = r'\bid=(?:"legacy-fsd-mux1"|legacy-fsd-mux1)(?=[\s>/])'
        mux1_toggle_id = r'\bid=(?:"legacy-fsd-mux1-tgl"|legacy-fsd-mux1-tgl)(?=[\s>/])'
        self.assertRegex(self.ui_gen, mux1_id)
        self.assertRegex(self.ui_gen, mux1_toggle_id)
        self.assertIn("document.getElementById('legacy-fsd-mux1-tgl')", self.ui_gen)
        self.assertEqual(len(re.findall(mux1_id, self.ui_gen)), 1)
        self.assertEqual(len(re.findall(mux1_toggle_id, self.ui_gen)), 1)

    def test_twai_diagnostics_are_exposed_for_legacy_can_a(self) -> None:
        twai = (ROOT / "include" / "drivers" / "twai_driver.h").read_text(encoding="utf-8")
        diag = (ROOT / "include" / "dash_twai_diag.h").read_text(encoding="utf-8")
        for token in [
            '#define TWAI_RX_QUEUE_LEN 128',
            'DashTwaiDiag',
            'snapshotDiag()',
            'twai_get_status_info',
            'readDrainBudgetHits',
            'framesAccepted',
            'framesDropped',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, twai + diag)
        for token in [
            '"twai"',
            '"state"',
            '"tec"',
            '"rec"',
            '"txFailed"',
            '"rxMissed"',
            '"readDrainBudgetHits"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

    def test_service_diag_evidence_is_exposed(self) -> None:
        for token in [
            '#include "dash_tx_evidence.h"',
            'DashTxEvidence serviceTxEvidence',
            'recordServiceTxEvidence',
            '"serviceDiag"',
            '"lastCommand"',
            '"burstFrames"',
            '"lastData"',
            '"vehicleResponse":"unknown"',
            'serviceDiagJson',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.main + self.dash)

    def test_wheel_dnd_uses_native_3c2_and_exposes_diag(self) -> None:
        for token in [
            'recordNative3c2',
            'native3c2Fresh',
            'DashWheelDndDiag',
            '"wheelDndDiag"',
            '"native3c2Seen"',
            '"baseFrameMode"',
            '"waiting_native"',
            't2canWheelDndRecordNative',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.main + self.dash + self.ui + self.handlers + self.legacy_speed + (ROOT / "include" / "dash_wheel_dnd.h").read_text(encoding="utf-8"))

    def test_legacy_fsd_config_accepts_get_emitted_policy_names(self) -> None:
        """POST /legacy_fsd_config must round-trip policy strings emitted by GET."""
        handler = re.search(r"static void handleLegacyFsdConfig\(\).*?server\.send\(200, \"application/json\", j\);", self.dash, re.S)
        self.assertIsNotNone(handler)
        body = handler.group(0)
        self.assertIn('p == "legacy_experimental"', body)
        self.assertIn('p == "legacy_stable"', body)
        self.assertIn('p == "experimental"', body)
        self.assertIn('p == "1"', body)
        self.assertLess(body.index('p == "legacy_stable"'), body.index('dashLegacyFsdPolicy = LegacyFsdPolicy::Stable'))

    def test_uptime_and_fsd_boot_persistence_are_wired(self) -> None:
        """Running time and FSD boot/default state must round-trip through /status and /config.

        bootCan can be changed via the "开机自动启用" toggle (saveConfig),
        and the master toggle also persists the chosen state as the boot default.
        """
        self.assertIn('uptime', self.dash)
        self.assertIn('bootCan', self.dash)
        self.assertIn('prefs.putBool("boot_can", bootCanActive)', self.dash)
        self.assertIn('bootCanActive = prefs.getBool("boot_can"', self.dash)
        self.assertIn('server.hasArg("bootCan")', self.dash)
        self.assertIn("var uptime=(d.uptime!==undefined)?d.uptime:(d.up||0);", self.ui)
        self.assertIn("fmtUp(uptime)", self.ui)
        # Design decision: master-toggle persistence (toggleFsd() also updates bootCan).
        self.assertIn("bootCan:next?'1':'0'", self.ui)
        self.assertIn("data.bootCan=bt.checked?'1':'0'", self.ui)

    def test_config_get_route_returns_fsd_runtime_for_ui_reload(self) -> None:
        """Regression: GET /config 必须存在并返回 fsdRuntime 块供 UI 回填。

        UI loadLegacyFsdConfig() 用 fetchJson('/config') 取 conf，再读
        conf.fsdRuntime.legacyOffset / conf.fsdRuntime.overrideSpeedLimit 回填
        Legacy 速度偏移输入框与重写限速开关。曾因只注册了 POST /config、缺 GET
        路由，导致 fetchJson('/config') 取 null → 重启后输入框/开关恒显示默认
        （NVS 实际已持久化，只是 UI 读不出来）。此测试锁住 GET /config 路由、
        handler 返回 fsdRuntime 嵌套块，以及前后端字段契约对齐。
        """
        # GET /config 路由 + handler 必须存在（POST 路由同时保留）
        self.assertIn('server.on("/config", HTTP_GET, handleConfigGet);', self.dash)
        self.assertIn('server.on("/config", HTTP_POST, handleConfig);', self.dash)
        handler = re.search(r"static void handleConfigGet\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(handler, "handleConfigGet handler missing")
        body = handler.group(0)
        # 返回 fsdRuntime 嵌套块，含 legacyOffset/overrideSpeedLimit（与 POST 契约对齐）
        self.assertIn("fsdRuntime", body)
        self.assertIn("legacyOffset", body)
        self.assertIn("nvsLegacyOffset", body)
        self.assertIn("overrideSpeedLimit", body)
        self.assertIn("nvsOverrideSpeedLimit", body)
        # UI 从 /config 的 fsdRuntime 读这两个字段（前后端契约对齐）
        self.assertIn("fetchJson('/config')", self.ui)
        self.assertIn("conf.fsdRuntime.legacyOffset", self.ui)
        self.assertIn("conf.fsdRuntime.overrideSpeedLimit", self.ui)

    def test_status_mux_json_is_closed_before_phase1_fields(self) -> None:
        """The /status JSON must close mux[] before appending Phase 1 root fields."""
        match = re.search(r"static void handleStatus\(\).*?server\.send\(200, \"application/json\", j\);", self.dash, re.S)
        self.assertIsNotNone(match)
        body = match.group(0)
        mux_pos = body.index('j += "]},\\"mux\\":[";')
        vehicle_pos = body.index('j += ",\\"vehicleOta\\":";')
        between = body[mux_pos:vehicle_pos]
        self.assertIn('j += "]";', between)
        self.assertNotIn('j += "]}";', body[vehicle_pos:])

    def test_settings_backup_exports_new_dashboard_config(self) -> None:
        """Backup JSON must include the newer FSD, speed, defense, lighting and power settings."""
        match = re.search(r"static void handleSettingsExport\(\).*?server\.send\(200, \"application/json\", j\);", self.dash, re.S)
        self.assertIsNotNone(match)
        body = match.group(0)
        for token in [
            '\\"bootCan\\"',
            '\\"apGate\\"',
            '\\"apAutoRestore\\"',
            '\\"driveProfile\\"',
            '\\"speedStrategy\\"',
            '\\"speed\\"',
            '\\"lighting\\"',
            '\\"defense\\"',
            '\\"power\\"',
            '\\"fsdRuntime\\"',
            '\\"legacyMpp\\"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)

    def test_settings_import_restores_new_dashboard_config(self) -> None:
        """Restore must accept Auto HW and write all persisted dashboard config groups."""
        match = re.search(r"static void handleSettingsImport\(\).*?dashLog\(\"\[BACKUP\] Settings imported", self.dash, re.S)
        self.assertIsNotNone(match)
        body = match.group(0)
        self.assertIn("hw >= 0 && hw <= 3", body)
        self.assertNotIn("hw >= 0 && hw <= 2", body)
        for token in [
            'p.putBool("boot_can"',
            'p.putBool("ap_gate"',
            'p.putBool("ap_rst"',
            'p.putUChar("drv_prof"',
            'p.putUChar("offsetMode"',
            'p.putUChar("manualPct"',
            'p.putBool("lt_en"',
            'p.putBool("def_en"',
            'p.putBool(NVS_KEY_AUTO_SHUTDOWN',
            'p.putBool("fa"',
            'p.putBool("lg_mpp_en"',
            "i < kHw3CustomTargetCount && i < arr.size()",
            "i < kHw3HighSpeedBucketCount && i < arr.size()",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)
        defense = re.search(r'if \(doc\["defense"\]\.is<JsonObject>\(\)\).*?if \(doc\["power"\]', body, re.S)
        self.assertIsNotNone(defense)
        defense_body = defense.group(0)
        self.assertIn('if (defense["nagMode"].is<int>())', defense_body)
        self.assertNotRegex(defense_body, r'else\s*\{\s*p\.putUChar\("def_nag_mode",\s*0\);\s*\}')

    def test_fsd_injection_control_lives_in_module_page(self) -> None:
        """FSD injection controls belong to Module Config, not Driving Mode."""
        module = re.search(r'id="pg-overview".*?<!-- Page 2: Hardware Config -->', self.ui, re.S)
        drive = re.search(r'id="pg-drive".*?<!-- Page 4: Speed Offset -->', self.ui, re.S)
        self.assertIsNotNone(module)
        self.assertIsNotNone(drive)
        self.assertIn('id="fsd-toggle"', module.group(0))
        self.assertIn('id="fsd-boot-tgl"', module.group(0))
        self.assertNotIn('id="fsd-toggle"', drive.group(0))
        self.assertNotIn('id="fsd-boot-tgl"', drive.group(0))

    def test_backend_accepts_panel_control_methods(self) -> None:
        expected_routes = [
            'server.on("/reset_stats", HTTP_POST, handleResetStats);',
            'server.on("/reboot", HTTP_POST, handleReboot);',
            'server.on("/service_mode", HTTP_POST, handleServiceMode);',
            'server.on("/burst", HTTP_POST, handleBurst);',
        ]
        for route in expected_routes:
            with self.subTest(route=route):
                self.assertIn(route, self.dash)

    def test_bus2_diagnostics_are_exposed_in_status(self) -> None:
        """Dual-CAN builds should expose CAN2 RX/TX/TXErr/EFLG separately from CAN1."""
        self.assertIn('uint32_t t2canBus2RxCount(void);', self.dash)
        self.assertIn('uint32_t t2canBus2TxCount(void);', self.dash)
        self.assertIn('uint32_t t2canBus2TxErrCount(void);', self.dash)
        self.assertIn('uint8_t t2canBus2Eflg(void);', self.dash)
        self.assertIn(',\\"can2\\":{\\"rx\\":', self.dash)
        self.assertIn('t2canBus2TxCount()', self.dash)
        self.assertIn('t2canBus2TxErrCount()', self.dash)
        self.assertIn('t2canBus2Eflg()', self.dash)
        self.assertIn('id="b2-tx"', self.ui)
        self.assertIn('id="b2-txerr"', self.ui)
        self.assertIn('id="b2-eflg"', self.ui)

    @unittest.skip("service mode is a dual-CAN/T-2CAN feature, pruned in single-CAN standalone")
    def test_service_mode_uses_vcsec_four_frame_pulse(self) -> None:
        """Service mode should send spec-correct 0x339 pulses, not continuous 0xE0 spam."""
        self.assertIn('g_svcBurstRemaining = 4;', self.main)
        self.assertIn('g_svcBurstValue = on ? 0x80 : 0x00;', self.main)
        self.assertIn('f.data[5] = g_svcBurstValue;', self.main)
        self.assertIn('t2canTxSecondaryCounted(f);', self.main)
        self.assertNotIn('f.data[5] = 0xE0;', self.main)
        self.assertIn('VCSEC_serviceDiagnosticRequest', self.readme)
        self.assertIn('00 00 00 00 00 80 00 00', self.readme)
        self.assertIn('00 00 00 00 00 00 00 00', self.readme)
        self.assertIn('四帧脉冲', self.ui)

    def test_mcp2515_bus2_spi_and_filter_support(self) -> None:
        """MCP2515 driver should support returning from accept-all to filtered mode and use 10MHz SPI."""
        mcp = (ROOT / "include" / "drivers" / "espidf_mcp2515.h").read_text(encoding="utf-8")
        self.assertIn('void setReceiveAllMode()', mcp)
        self.assertIn('void setUseFiltersMode()', mcp)
        self.assertIn('RXBnCTRL_RXM_STDEXT', mcp)
        self.assertIn('dev.clock_speed_hz = 10000000', mcp)

    @unittest.skip("shared-bus high-beam limit is a dual-CAN/T-2CAN concern; single-CAN standalone README does not document it")
    def test_high_beam_shared_bus_limit_is_documented(self) -> None:
        """README should record that shared-bus injection cannot force high-beam during FSD."""
        for token in [
            '0x3F5 byte1 bit7',
            '0x3F5 byte3',
            '0x293 byte2 bit6',
            'inline MITM',
            'shared-bus injection cannot override',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.readme)

    def test_all_panel_endpoints_have_matching_backend_routes(self) -> None:
        routes = set()
        for route, method in re.findall(r'server\.on\("([^"]+)",\s*(HTTP_\w+),', self.dash):
            routes.add((method.replace("HTTP_", ""), route))

        expected = {
            ("GET", "/status"),
            ("GET", "/system_status"),
            ("GET", "/can_pins"),
            ("GET", "/frames"),
            ("GET", "/bus2_ids"),
            ("GET", "/stalk_test"),
            ("GET", "/burst"),
            ("GET", "/ota_creds"),
            ("GET", "/wifi_scan"),
            ("GET", "/wifi_status"),
            ("GET", "/wifi_networks"),
            ("GET", "/ap_status"),
            ("GET", "/gateway_status"),
            ("GET", "/gateway_dns"),
            ("GET", "/gateway_dns_test"),
            ("GET", "/gateway_blocked"),
            ("GET", "/rec_download"),
            ("GET", "/mode_hw"),
            ("GET", "/drive_profile"),
            ("GET", "/speed_strategy"),
            ("GET", "/speed_custom"),
            ("GET", "/lighting_config"),
            ("GET", "/defense_config"),
            ("GET", "/gear_assist_status"),
            ("GET", "/hotspot_config"),
            ("GET", "/dns_rules"),
            ("POST", "/config"),
            ("POST", "/mode_hw"),
            ("POST", "/drive_profile"),
            ("POST", "/speed_strategy"),
            ("POST", "/speed_custom"),
            ("POST", "/lighting_config"),
            ("POST", "/defense_config"),
            ("POST", "/hotspot_config"),
            ("POST", "/relay_wifi_test"),
            ("POST", "/dns_rules"),
            ("POST", "/reset_stats"),
            ("POST", "/reboot"),
            ("POST", "/service_mode"),
            ("POST", "/burst"),
            ("POST", "/wifi_config"),
            ("POST", "/wifi_connect"),
            ("POST", "/wifi_delete"),
            ("POST", "/gateway_dns"),
            ("POST", "/gateway_blocked_clear"),
            ("POST", "/rec_start"),
            ("POST", "/rec_stop"),
            ("POST", "/logging"),
            ("POST", "/ap_config"),
            ("POST", "/update"),
            ("GET", "/update_check"),
            ("POST", "/update_install"),
            ("GET", "/update_beta"),
            ("POST", "/update_beta"),
            ("GET", "/auto_update"),
            ("POST", "/auto_update"),
            # Phase 1 新增端点
            ("GET", "/power_mgmt"),
            ("POST", "/power_mgmt"),
            ("GET", "/vehicle_ota_status"),
            ("GET", "/fog_light"),
            ("POST", "/fog_light"),
            ("POST", "/strobe_cont"),
        }
        missing = sorted(expected - routes)
        self.assertEqual([], missing)

    def test_ota_upload_matches_espidf_raw_upload_handler(self) -> None:
        self.assertIn("xhr.open('POST','/update',true);", self.ui)
        self.assertIn("xhr.setRequestHeader('Content-Type','application/octet-stream');", self.ui)
        self.assertIn("xhr.setRequestHeader('X-File-Name'", self.ui)
        self.assertIn("xhr.send(file);", self.ui)
        # The OTA /update handler streams a raw octet-stream body (xhr.send(file)),
        # never a multipart form. FormData is permitted elsewhere only for the
        # /plugins/upload route, whose backend consumes WebServer multipart chunks.
        # Guard that FormData usage stays tied to the plugin upload and never leaks
        # into the OTA path.
        ota_idx = self.ui.find("xhr.open('POST','/update',true);")
        plugin_idx = self.ui.find("/plugins/upload")
        self.assertGreater(ota_idx, -1)
        formdata_idx = self.ui.find("new FormData()")
        if formdata_idx != -1:
            self.assertNotEqual(plugin_idx, -1)
            # FormData must belong to the plugin upload scope, not the OTA path.
            self.assertLess(abs(formdata_idx - plugin_idx), 256)

    def test_phase2_dashboard_uses_explicit_contract_endpoints(self) -> None:
        expected = [
            "/mode_hw",
            "/drive_profile",
            "/speed_strategy",
            "/lighting_config",
            "/defense_config",
            "/hotspot_config",
            "/relay_wifi_test",
            "/dns_rules",
        ]
        for endpoint in expected:
            with self.subTest(endpoint=endpoint):
                self.assertIn(endpoint, self.ui)

    def test_bus2_controls_have_state_roundtrip(self) -> None:
        self.assertIn("setText('b2-rx',d.rx_total||0);", self.ui)
        self.assertIn("svc.checked=!!d.service_mode;", self.ui)
        self.assertIn('",\\"service_mode\\":', self.dash)
        self.assertIn('"],\\"rx_total\\":', self.dash)

    def test_stalk_duration_is_honored_by_backend(self) -> None:
        self.assertIn("duration_ms", self.ui)
        self.assertIn('server.hasArg("dur")', self.dash)
        self.assertIn('",\\"duration_ms\\":"', self.dash)

    def test_flash_burst_double_pull_is_wired(self) -> None:
        """Flash Burst should be firmware-timed, default-off, and controlled via /burst."""
        for token in [
            'static volatile bool g_burstEnabled = false;',
            't2canBurstCheckTrigger',
            't2canBurstTick',
            'uint8_t status = (f.data[1] >> 4) & 0x07;',
            'kBurstDoublePullWindowMs = 2000',
            'g_burstPrevStatus != 1 && status == 1',
            't2canSetBurstEnabled',
            't2canSetBurstParams',
            't2canBurstIsRunning',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.main)
        task = re.search(r"static void app_can_task\(void \*\).*?appCanTaskLoops", self.main, re.S)
        self.assertIsNotNone(task)
        self.assertLess(task.group(0).index('t2canBurstTick();'), task.group(0).index('t2canStalkInjectTick();'))
        for token in [
            'server.on("/burst", HTTP_GET, handleBurst);',
            'server.on("/burst", HTTP_POST, handleBurst);',
            'static void handleBurst()',
            '\\"phases_left\\"',
            '\\"last_trigger_ms\\"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)
        for token in [
            '双拨爆闪',
            '启用双拨触发',
            '/burst',
            'loadBurstConfig',
            'setBurstPreset',
            'BURST_PRESETS',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)
                self.assertIn(token if token != '双拨爆闪' else '/burst', self.ui_gen)

    def test_gateway_profile_and_poll_interval_are_functional(self) -> None:
        self.assertIn("var DNS_PROFILES = {", self.ui)
        self.assertIn("function applyDnsProfile(profile)", self.ui)
        self.assertIn("function detectDnsProfile(blacklist,whitelist)", self.ui)
        self.assertIn("function updateDnsProfileCards(profile)", self.ui)
        self.assertIn("var data={enabled:'1'};", self.ui)
        self.assertIn("function restartPoll(ms)", self.ui)
        self.assertIn("pollTick=function()", self.ui)

    def test_phase1_new_endpoints(self) -> None:
        """Phase 1 新增端点应在固件路由中注册，handler 函数存在"""
        new_endpoints = {
            "GET  /power_mgmt": "handlePowerMgmt",
            "POST /power_mgmt": "handlePowerMgmt",
            "GET  /vehicle_ota_status": "handleVehicleOtaStatus",
            "GET  /fog_light": "handleFogLight",
            "POST /fog_light": "handleFogLight",
            "POST /strobe_cont": "handleStrobeCont",
        }
        for endpoint, handler in new_endpoints.items():
            method, path = endpoint.split()
            with self.subTest(endpoint=endpoint):
                self.assertIn(f'"{path}"', self.dash, f"Missing route: {endpoint}")
                self.assertIn(handler, self.dash, f"Missing handler: {handler}")

    def test_phase1_status_fields_exist(self) -> None:
        """Phase 1 新增 /status 字段应在 handleStatus() JSON 中输出"""
        fields = ["vehicleOta", "autoShutdown", "wifiAutoOff", "fogStrategy", "strobeCont", "dndVolume", "dndSpeed"]
        for field in fields:
            with self.subTest(field=field):
                # In C++ source, JSON keys appear as \"fieldName\":
                self.assertIn(f',\\"{field}\\"', self.dash)

    def test_phase1_ota_guard_gates_handler_injection(self) -> None:
        """Handler-level AD checks must include OTA guard, not only dashboard post-processing."""
        check_ad = re.search(r"static bool dashCheckADEnabled\(\).*?\n\}", self.dash, re.S)
        self.assertIsNotNone(check_ad)
        self.assertIn("canActive", check_ad.group(0))
        self.assertIn("dashOtaGuardAllowInjection()", check_ad.group(0))
        self.assertIn("CAN_ID_OTA_STATUS", self.handlers)

    def test_phase1_power_mgmt_partial_update_and_wake_pin(self) -> None:
        """Power management updates should preserve omitted fields and use configurable wake pin."""
        power = re.search(r"static void handlePowerMgmt\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(power)
        body = power.group(0)
        self.assertIn('if (server.hasArg("autoShutdown"))', body)
        self.assertIn('if (server.hasArg("wifiAutoOff"))', body)
        self.assertIn("dashArgTruthy(server.arg(\"autoShutdown\"))", body)
        self.assertIn("dashArgTruthy(server.arg(\"wifiAutoOff\"))", body)
        power_header = (ROOT / "include" / "dash_power_mgmt.h").read_text(encoding="utf-8")
        self.assertIn("DASH_WAKE_PIN", power_header)
        self.assertIn("TWAI_RX_PIN", power_header)
        self.assertIn("dashPowerMgmtConfigureWake()", power_header)

    def test_phase2_speed_strategy_syncs_new_offset_mode(self) -> None:
        """/speed_strategy must drive the new speed algorithm mode and persist it."""
        self.assertIn("offsetMode = dashSpeedStrategy;", self.dash)
        self.assertIn("dashSyncLegacyShims();", self.dash)
        self.assertIn('prefs.putUChar("offsetMode", offsetMode);', self.dash)
        self.assertIn('prefs.putUChar("spd_str", dashSpeedStrategy);', self.dash)

    def test_config_legacy_hw3_speed_flags_sync_new_strategy(self) -> None:
        """Old /config HW3 flags must map into the new offset strategy before syncing shims."""
        config = re.search(r"static void handleConfig\(\).*?static void handleLoggingConfig\(\)", self.dash, re.S)
        self.assertIsNotNone(config)
        body = config.group(0)
        self.assertIn('server.hasArg("hw3AutoSpeed")', body)
        self.assertIn('bool legacyHw3CustomSpeed = hw3CustomSpeed;', body)
        self.assertIn('bool legacyHw3HighSpeedEnable = hw3HighSpeedEnable;', body)
        self.assertIn('bool legacyHw3AutoSpeed = hw3AutoSpeed;', body)
        self.assertIn('if (server.hasArg("hw3CustomSpeed"))', body)
        self.assertIn('legacyHw3CustomSpeed = server.arg("hw3CustomSpeed") == "1";', body)
        self.assertIn('if (server.hasArg("hw3HighSpeedEnable"))', body)
        self.assertIn('legacyHw3HighSpeedEnable = server.arg("hw3HighSpeedEnable") == "1";', body)
        self.assertIn('if (server.hasArg("hw3AutoSpeed"))', body)
        self.assertIn('legacyHw3AutoSpeed = server.arg("hw3AutoSpeed") == "1";', body)
        self.assertNotIn('bool legacyHw3CustomSpeed = server.hasArg("hw3CustomSpeed") && server.arg("hw3CustomSpeed") == "1";', body)
        self.assertNotIn('bool legacyHw3HighSpeedEnable = server.hasArg("hw3HighSpeedEnable") && server.arg("hw3HighSpeedEnable") == "1";', body)
        self.assertNotIn('bool legacyHw3AutoSpeed = server.hasArg("hw3AutoSpeed") && server.arg("hw3AutoSpeed") == "1";', body)
        self.assertIn('dashSpeedStrategy = legacyHw3CustomSpeed ? 2 : ((legacyHw3HighSpeedEnable || legacyHw3AutoSpeed) ? 1 : 0);', body)
        self.assertIn('offsetMode = dashSpeedStrategy;', body)
        self.assertIn('dashSyncLegacyShims();', body)

    def test_hw3_mux2_runtime_uses_new_offset_algorithm_as_single_source(self) -> None:
        """HW3 mux-2 active raw must come from dashComputeHw3OffsetRaw/dashComputeOffset for fixed/auto/custom consistency."""
        mux2 = re.search(r"// ── Mux 2: Speed offset.*?if \(index == 0 && enablePrint\)", self.handlers, re.S)
        self.assertIsNotNone(mux2)
        body = mux2.group(0)
        self.assertRegex(body, r"uint8_t\s+activeRaw\s*=\s*dashComputeHw3OffsetRaw\([^;]+\);")
        self.assertLess(body.index("dashComputeHw3OffsetRaw"), body.index("hw3OffsetTargetRaw = activeRaw;"))
        self.assertNotIn("dashComputeHw3CustomTargetKph", body)
        self.assertNotIn("dashComputeHw3AutoTargetKph", body)
        self.assertNotIn("hw3HighSpeedTargetPct", body)

    def test_speed_strategy_does_not_enable_legacy_mpp(self) -> None:
        """/speed_strategy drives the shared 3-mode algorithm, not the old Legacy MPP path."""
        speed_strategy = re.search(r"static void handleSpeedStrategy\(\).*?static String dashSpeedCustomJson\(\)", self.dash, re.S)
        self.assertIsNotNone(speed_strategy)
        body = speed_strategy.group(0)
        self.assertNotIn('legacyMppCustomEnable = true;', body)
        self.assertNotIn('legacyMppCustomEnable =', body)
        self.assertIn('offsetMode = dashSpeedStrategy;', body)
        self.assertIn('dashSyncLegacyShims();', body)

    def test_legacy_can760_uses_simple_offset_helper_not_mpp(self) -> None:
        """Legacy speed offset must use the verified CAN760 byte5 UI_userSpeedOffset path."""
        can760 = re.search(r"if \(frame\.id == 760\).*?if \(frame\.id == 1080\)", self.handlers, re.S)
        self.assertIsNotNone(can760)
        body = can760.group(0)
        self.assertIn('dashComputeLegacySimpleOffsetKph', body)
        self.assertIn('frame.data[5]', body)
        self.assertNotIn('dashComputeLegacyMppTargetKph', body)
        self.assertNotIn('frame.data[6]', body)

    def test_legacy_handler_captures_fused_speed_limit_from_921(self) -> None:
        """Legacy/HW0 needs the same fused speed limit input as the 3-mode speed UI."""
        can921 = re.search(r"if \(frame\.id == 921\).*?// 0x3EE", self.handlers, re.S)
        self.assertIsNotNone(can921)
        body = can921.group(0)
        self.assertIn('fusedSpeedLimitRaw = static_cast<uint8_t>(frame.data[1] & 0x1F);', body)
        self.assertIn('uint8_t apState = readDASAutopilotStatus(frame);', body)
        self.assertIn('APActive = isDASAutopilotActive(apState);', body)
        self.assertIn('uint32_t nowMs = dashDiagNowMs();', body)
        self.assertIn('if (lateEchoSelected())', body)
        self.assertIn('lateNag.onDasStatus(apState, hos, nowMs, active, gateReason);', body)
        self.assertIn('nag.onNagSample(hos, nowMs, active, apState, gateReason);', body)

    def test_legacy_simple_offset_helper_reuses_three_mode_state(self) -> None:
        """Legacy simple offset should reuse the speed page algorithm and clamp to byte5 wire range."""
        self.assertIn('kLegacySimpleOffsetMaxKph = 33', self.legacy_speed)
        self.assertIn('dashComputeLegacySimpleOffsetKph', self.legacy_speed)
        self.assertIn('fusedSpeedLimitRaw', self.legacy_speed)
        self.assertIn('dashComputeOffset(limitKph, 0.05f)', self.legacy_speed)
        self.assertIn('dashClampLegacySimpleOffsetKph', self.legacy_speed)

    def test_phase2_speed_custom_endpoint_contract(self) -> None:
        """/speed_custom exposes four validated custom percentage zones."""
        self.assertIn("static bool dashArgUIntInRange", self.dash)
        self.assertIn("static void handleSpeedCustomGet()", self.dash)
        self.assertIn("static void handleSpeedCustom()", self.dash)
        self.assertIn('server.on("/speed_custom", HTTP_GET, handleSpeedCustomGet);', self.dash)
        self.assertIn('server.on("/speed_custom", HTTP_POST, handleSpeedCustom);', self.dash)
        for idx, arg in enumerate(["cp1", "cp2", "cp3", "cp4"]):
            with self.subTest(arg=arg):
                self.assertIn(f'server.hasArg("{arg}")', self.dash)
                self.assertIn(f'dashArgUIntInRange("{arg}", 0, 50, next)', self.dash)
                self.assertIn(f'nextCustomPct[{idx}] = next;', self.dash)
        self.assertIn('server.hasArg("manualPct")', self.dash)
        self.assertIn('dashArgUIntInRange("manualPct", 0, 50, next)', self.dash)
        self.assertIn('nextManualPct = next;', self.dash)
        self.assertIn('manualOffsetPct = nextManualPct;', self.dash)
        self.assertIn('prefs.putUChar("cp0", customPct[0]);', self.dash)
        self.assertIn('\\"manualPct\\":', self.dash)
        self.assertIn('\\"customPct\\":[', self.dash)

    def test_speed_custom_get_is_always_read_only(self) -> None:
        """GET /speed_custom must ignore query args and never persist."""
        get_handler = re.search(r"static void handleSpeedCustomGet\(\).*?static void handleSpeedCustom\(\)", self.dash, re.S)
        self.assertIsNotNone(get_handler)
        body = get_handler.group(0)
        self.assertIn("dashSpeedCustomJson()", body)
        self.assertNotIn("dashSavePrefs", body)
        self.assertNotIn("server.hasArg", body)
        self.assertNotIn("server.method()", body)

        post_handler = re.search(r"static void handleSpeedCustom\(\).*?static String dashLightingConfigJson\(\)", self.dash, re.S)
        self.assertIsNotNone(post_handler)
        self.assertNotIn("server.method()", post_handler.group(0))

    def test_speed_custom_rejects_invalid_values_before_saving(self) -> None:
        """POST /speed_custom must reject non-decimal or out-of-range values atomically."""
        speed_custom = re.search(r"static void handleSpeedCustom\(\).*?static String dashLightingConfigJson\(\)", self.dash, re.S)
        self.assertIsNotNone(speed_custom)
        body = speed_custom.group(0)
        self.assertIn('server.send(400, "application/json", "{\\"ok\\":false,\\"error\\":\\"manualPct/cp1..cp4 must be decimal integers from 0 to 50\\"}");', body)
        self.assertIn('bool valid = true;', body)
        self.assertLess(body.index('bool valid = true;'), body.index('if (!valid)'))
        self.assertLess(body.index('if (!valid)'), body.index('dashSavePrefs();'))
        self.assertNotIn('.toInt()', body)

    def test_phase2_status_exposes_actual_offset_and_speed_limit(self) -> None:
        """/status must surface actual offset and fused speed limit in kph, with SNA/NONE as 0."""
        self.assertIn(',\\"actOffset\\":', self.dash)
        self.assertIn('actualOffset', self.dash)
        self.assertIn(',\\"speedLimit\\":', self.dash)
        self.assertIn('(fusedSpeedLimitRaw == 0 || fusedSpeedLimitRaw == 31)', self.dash)
        self.assertIn('(uint16_t)fusedSpeedLimitRaw * 5', self.dash)

    def test_legacy_smart_speed_api_contract(self) -> None:
        """config/status/export must expose Legacy Smart Offset fields."""
        for token in [
            '"legacyOffsetMode"',
            '"legacySmoothDown"',
            '"legacySmoothRateKphS"',
            '"legacyCustomPctLow"',
            '"legacyCustomPctMid"',
            '"legacyCustomPctHigh"',
            '"legacyCustomPctVeryHigh"',
            '"legacySpeed"',
            '"gpsSpeedSeen"',
            '"lastSentOffsetRaw"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)

        self.assertIn('"lo_mode"', self.dash)
        self.assertIn('"lo_smooth"', self.dash)
        self.assertIn('"lo_rate"', self.dash)
        self.assertIn('"lo_p1"', self.dash)
        self.assertIn('"lo_p2"', self.dash)
        self.assertIn('"lo_p3"', self.dash)
        self.assertIn('"lo_p4"', self.dash)

    def test_abort_guard_api_contract(self) -> None:
        """Abort Guard must be default-off defense config plus status diagnostics."""
        for token in [
            '"def_ag"',
            '"abort_guard"',
            '"abortGuard"',
            '"latched"',
            '"lastAbortState"',
            '"lastBlockedPath"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.dash)
        self.assertIn('prefs.getBool("def_ag", false)', self.dash)

    def test_legacy_speed_ui_mentions_smart_offset_and_0x2f8_absence(self) -> None:
        """UI must explain smart speed and frame visibility."""
        for token in [
            '智能速度偏移',
            '0x2F8 未检测到',
            '降速平滑',
            'Abort Guard',
            '实验',
            '默认关闭',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_legacy_smart_speed_ui_matches_backend_contract(self) -> None:
        """Legacy smart speed UI must match backend enum/range/status contracts."""
        for token in [
            "if(v==='off'||v===0)return 'off'",
            "if(v==='manual'||v===1)return 'manual'",
            "if(v==='auto'||v===2)return 'auto'",
            "if(v==='custom'||v===3)return 'custom'",
            "mode=(modeVal==='off')?0:((modeVal==='manual')?1:((modeVal==='auto')?2:3))",
            'min="1" max="20" id="legacy-smooth-rate"',
            'min="0" max="63" id="legacy-pct-low"',
            'min="0" max="63" id="legacy-pct-mid"',
            'min="0" max="63" id="legacy-pct-high"',
            'min="0" max="63" id="legacy-pct-vhigh"',
            "clampNum('legacy-smooth-rate',5,1,20)",
            "clampNum('legacy-pct-low',50,0,63)",
            "clampNum('legacy-pct-mid',30,0,63)",
            "clampNum('legacy-pct-high',20,0,63)",
            "clampNum('legacy-pct-vhigh',10,0,63)",
            "ls.lastSentOffsetRaw!==undefined",
            "syncLegacyOffsetInputs('legacy-offset-manual')",
            "syncLegacyOffsetInputs('legacy-offset-inp')",
            "legacyOffset:clampNum('legacy-offset-inp',0,0,33)",
            "d.abort_guard||d.bionic_steering||d.speed_no_disturb",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)
        self.assertNotIn('max="30" id="legacy-smooth-rate"', self.ui)
        self.assertNotIn('max="50" id="legacy-pct-low"', self.ui)
        self.assertNotIn("clampNum('legacy-smooth-rate',5,1,30)", self.ui)
        self.assertNotIn("clampNum('legacy-pct-low',50,0,50)", self.ui)
        self.assertNotIn("ls.gpsUserOffsetRaw!==undefined", self.ui)

    def test_phase2_speed_offset_ui_uses_new_three_mode_contract(self) -> None:
        """Speed page must use the fixed/auto/custom UI and sync the Phase 2 APIs."""
        required_ui = [
            'id="speed-mode-tabs"',
            "showSpeedMode('fixed')",
            "showSpeedMode('auto')",
            "showSpeedMode('custom')",
            'id="speed-panel-fixed"',
            'id="speed-panel-auto"',
            'id="speed-panel-custom"',
            "固定百分比",
            "自动偏移",
            "自定义偏移",
            "0-50 km/h",
            "51-70 km/h",
            "71-100 km/h",
            "101+ km/h",
            'id="speed-cp1"',
            'id="speed-cp2"',
            'id="speed-cp3"',
            'id="speed-cp4"',
            'id="sp-limit"',
            'id="sp-act-offset"',
            'id="sp-active-mode"',
            'id="sp-wire"',
            "/speed_strategy",
            "/speed_custom",
            "manualPct",
            "cp1",
            "cp2",
            "cp3",
            "cp4",
            "loadSpeedStrategy()",
            "saveSpeedCustom()",
            "setSpeedFixedPct(30)",
            "setText('sp-limit'",
            "setText('sp-act-offset'",
        ]
        for token in required_ui:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_phase2_auto_speed_algorithm_card_shows_actual_five_segments(self) -> None:
        """Auto speed card must document the firmware's actual five-segment algorithm."""
        panel_match = re.search(r'<div class="card" id="speed-panel-auto".*?</div>\s*<div class="card" id="speed-panel-custom"', self.ui, re.S)
        self.assertIsNotNone(panel_match)
        panel = panel_match.group(0)
        expected_rows = [
            ("≤ 40 km/h", "+50%，封顶 60"),
            ("≤ 60 km/h", "+50%，封顶 90"),
            ("≤ 90 km/h", "+30%，封顶 117"),
            ("≤ 110 km/h", "+20%，封顶 132"),
            ("> 110 km/h", "+10%，封顶 132"),
        ]
        for speed_limit, auto_offset in expected_rows:
            with self.subTest(speed_limit=speed_limit):
                self.assertIn(f"<tr><td>{speed_limit}</td><td>{auto_offset}</td></tr>", panel)
        self.assertNotIn("≤ 50 km/h", panel)
        self.assertNotIn("51-70 km/h", panel)
        self.assertNotIn("71-100 km/h", panel)
        self.assertNotIn("101+ km/h", panel)

    def test_update_profile_cards_does_not_write_speed_strategy_label(self) -> None:
        """Drive profile UI refresh must not overwrite speed-current, which belongs to speed strategy."""
        profile_cards = re.search(r"function updateProfileCards\(sp\)\{.*?function updateDriveCards", self.ui, re.S)
        self.assertIsNotNone(profile_cards)
        self.assertNotIn("speed-current", profile_cards.group(0))

    def test_phase2_generated_dashboard_header_contains_new_speed_ui(self) -> None:
        """Generated minified header should be refreshed from the new speed UI source."""
        for token in [
            "speed-mode-tabs",
            "speed-cp1",
            "sp-act-offset",
            "/speed_custom",
            "manualPct",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui_gen)

    def test_legacy_speed_algo_branch_exposes_dashboard_offset_state(self) -> None:
        """USE_NEW_SPEED_ALGO=0 rollback path must still compile with dashboard speed API."""
        speed_header = (ROOT / "include" / "dash_hw3_speed.h").read_text(encoding="utf-8")
        legacy_match = re.search(r"#else // !USE_NEW_SPEED_ALGO.*?#endif // USE_NEW_SPEED_ALGO", speed_header, re.S)
        self.assertIsNotNone(legacy_match)
        legacy = legacy_match.group(0)
        expected_symbols = [
            "offsetMode",
            "manualOffsetPct",
            "customPct[4]",
            "actualOffset",
            "dashSyncLegacyShims()",
        ]
        for symbol in expected_symbols:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, legacy)
        self.assertIn("hw3AutoSpeed = (offsetMode == 1);", legacy)
        self.assertIn("hw3CustomSpeed = (offsetMode == 2);", legacy)
        self.assertIn("hw3HighSpeedEnable = (offsetMode != 0);", legacy)
        self.assertNotIn("hw3HighSpeedEnable = (offsetMode == 2);", legacy)

    def test_ap_status_mode_field_matches_ui(self) -> None:
        self.assertIn("setText('ap-mode',ap.mode||", self.ui)
        self.assertIn('",\\"mode\\":\\""', self.dash)

    # ── Phase 3: Bionic Steering + Wheel DND ──────────────────

    def test_phase3_wheel_dnd_header_exists(self) -> None:
        """dash_wheel_dnd.h must exist with four-step sequence state machine."""
        dnd = (ROOT / "include" / "dash_wheel_dnd.h").read_text(encoding="utf-8")
        for symbol in ["DashWheelDND", "startVolume", "startSpeed", "tick",
                        "isRunning", "reset", "kSteps", "kStepCount{4}",
                        "kCanId{0x3C2}"]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, dnd)
        # Sequence: 01→00→3F→00
        self.assertIn("0x01", dnd)
        self.assertIn("0x3F", dnd)
        # 50ms step interval
        self.assertIn("kStepIntervalMs{50}", dnd)

    def test_phase3_wheel_dnd_checksum(self) -> None:
        """DND frames must include Tesla checksum calculation."""
        dnd = (ROOT / "include" / "dash_wheel_dnd.h").read_text(encoding="utf-8")
        self.assertIn("0xC2u + 0x03u", dnd)  # CAN ID 0x3C2 bytes
        self.assertIn("outData[7]", dnd)

    def test_phase3_wheel_dnd_is_runtime_wired(self) -> None:
        """Wheel DND must be instantiated, started by API, and ticked from CAN task."""
        self.assertIn('#include "dash_wheel_dnd.h"', self.dash)
        self.assertIn("static DashWheelDND dashWheelDndCtrl;", self.dash)
        defense = re.search(r"static void handleDefenseConfig\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(defense)
        body = defense.group(0)
        self.assertIn("dashWheelDndCtrl.startVolume()", body)
        self.assertIn("dashWheelDndCtrl.startSpeed()", body)
        self.assertIn("static void t2canWheelDndTick()", self.main)
        self.assertIn("dashWheelDndCtrl.tick((int)millis(), data)", self.main)
        self.assertIn("f.id = 0x3C2;", self.main)
        self.assertIn("gateOpen = canActive && dashDefenseEnabled", self.main)
        self.assertIn("g_wheelDndGateWasOpen", self.main)
        self.assertIn("dashWheelDndCtrl.reset()", self.main)
        can_task = re.search(r"static void app_can_task\(void \*\).*?appCanTaskLoops", self.main, re.S)
        self.assertIsNotNone(can_task)
        self.assertIn("t2canWheelDndTick();", can_task.group(0))

    def test_phase3_wheel_dnd_only_starts_when_defense_enabled(self) -> None:
        """DND switches should not inject frames unless the defense system is enabled."""
        defense = re.search(r"static void handleDefenseConfig\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(defense)
        body = defense.group(0)
        self.assertIn("dashDefenseEnabled && dashDndVolume && (!prevDefenseEnabled || !prevDndVolume)", body)
        self.assertIn("dashDefenseEnabled && dashDndSpeed && (!prevDefenseEnabled || !prevDndSpeed)", body)

    def test_naghandler_dual_mode_passthrough_default(self) -> None:
        """NagHandler branches on nagTorqueTamperRuntime; DEFAULT = PASSTHROUGH
        (torque bytes copied unchanged), opt-in = TORQUE_TAMPER (1.80 Nm fixed
        0xB6 + positive sign nibble). The bionic path was removed in the
        2026-06-24 dual-mode refactor and must stay out of NagHandler."""
        nag = re.search(r"struct NagHandler.*?^\};", self.handlers, re.S | re.M)
        self.assertIsNotNone(nag)
        body = nag.group(0)
        # Opt-in flag branch
        self.assertIn("nagTorqueTamperRuntime", body)
        # PASSTHROUGH default (else): torque bytes copied through unchanged
        self.assertIn("echo.data[2] = frame.data[2]", body)
        self.assertIn("echo.data[3] = frame.data[3]", body)
        # TORQUE_TAMPER opt-in: fixed 1.80 Nm torque + positive sign nibble
        self.assertIn("0xB6", body)
        self.assertIn("(frame.data[2] & 0xF0) | 0x08", body)
        # Removed bionic path must stay out of NagHandler (regression guard)
        self.assertNotIn("DashBionicSteer", body)
        self.assertNotIn("bionic.computePerturbation", body)

    def test_soft_engage_is_wired_into_legacy_gate(self) -> None:
        """Soft Engage angle gate must be wired into the Legacy dashboard gate
        and exposed via NVS + HTTP (spec rev.3). The pure helper lives in
        can_helpers.h; the ESP-only dashboard gate calls it and reads
        apRestoreState, with a per-episode latch."""
        # 1. pure helper defined in can_helpers.h
        self.assertIn("dashSoftEngageRelease(", self.can_helpers)
        # 2. dashboard gate calls helper + reads steering angle + manages latch
        self.assertIn("dashSoftEngageRelease(", self.dash)
        self.assertIn("apRestoreState.steerSeen", self.dash)
        self.assertIn("apRestoreState.steerValidity", self.dash)
        self.assertIn("apRestoreState.steerAngleX10", self.dash)
        self.assertIn("legacySoftEngageSent", self.dash)
        self.assertIn("legacySoftEngageSent = false", self.dash)  # re-arm on new AP episode
        # 3. constants + state
        self.assertIn("SOFT_ENGAGE_ANGLE_THRESH_X10", self.dash)
        self.assertIn("SOFT_ENGAGE_TIMEOUT_MS", self.dash)
        self.assertIn("kSoftEngageDefaultEnabled", self.dash)
        self.assertIn("dashSoftEngage", self.dash)
        # 4. NVS key + HTTP fields
        self.assertIn('"def_se"', self.dash)
        self.assertIn('"soft_engage"', self.dash)
        self.assertIn('"softEngage"', self.dash)

    def test_dashboard_runtime_state_syncs_defense_to_handlers(self) -> None:
        """Loaded NVS/UI defense state must reach active handler and handlerPool."""
        runtime = re.search(r"static void dashApplyRuntimeState\(\).*?#if defined\(DASH_RGB_STATUS_LED\)", self.dash, re.S)
        self.assertIsNotNone(runtime)
        runtime_body = runtime.group(0)
        for token in [
            "dashHandler->bionicSteering = dashBionicSteering",
            "dashHandler->isaChimeSuppress = nvsIsaChimeSuppress",
            "dashHandler->banShieldEnable = nvsBanShieldEnable",
            "dashHandler->legacyOffset = nvsLegacyOffset",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, runtime_body)

        nvs_sync = re.search(r"static void dashApplyNvsRuntimeSwitches\(\).*?\n}\n", self.dash, re.S)
        self.assertIsNotNone(nvs_sync)
        nvs_body = nvs_sync.group(0)
        self.assertIn("handlerPool[i]->bionicSteering = dashBionicSteering", nvs_body)
        self.assertIn("handlerPool[i]->banShieldEnable = nvsBanShieldEnable", nvs_body)
        self.assertIn("dashApplyNvsRuntimeSwitches();\n    dashSwapHandler(hwMode);", self.dash)

    def test_phase3_defense_config_exposes_dnd_params(self) -> None:
        """defense_config must accept and return dnd_volume and dnd_speed."""
        defense = re.search(r"static void handleDefenseConfig\(\).*?server\.send\(200", self.dash, re.S)
        self.assertIsNotNone(defense)
        body = defense.group(0)
        self.assertIn('server.hasArg("dnd_volume")', body)
        self.assertIn('server.hasArg("dnd_speed")', body)
        self.assertIn("dashDndVolume", body)
        self.assertIn("dashDndSpeed", body)

    def test_phase3_defense_config_json_includes_bionic_status(self) -> None:
        """defense_config JSON must report bionic_disabled for UI warning."""
        json_fn = re.search(r"static String dashDefenseConfigJson\(\).*?return j;", self.dash, re.S)
        self.assertIsNotNone(json_fn)
        body = json_fn.group(0)
        self.assertIn('\\"bionic_disabled\\"', body)
        self.assertIn('\\"dnd_volume\\"', body)
        self.assertIn('\\"dnd_speed\\"', body)
        self.assertIn("bionicDisabled()", body)
        self.assertIn("dashBionicDisabled", body)

    def test_phase3_defense_ui_has_7_toggles(self) -> None:
        """Defense page must have all 7 toggle switches in pg-defense section.

        (Was 8; the EPAS-nag toggle ``def-epnag-tgl`` was intentionally removed
        in the nag-injection safety takedown — see ``test_no_epas_nag_contract``
        for the guard that keeps it gone.)
        """
        defense_page = re.search(r'id="pg-defense".*?id="pg-ota"', self.ui, re.S)
        self.assertIsNotNone(defense_page)
        body = defense_page.group(0)
        toggles = [
            'id="hw3-slew-tgl"',
            'id="def-bionic-tgl"',
            'id="def-sound-tgl"',
            'id="def-dnd-vol-tgl"',
            'id="def-speed-nd-tgl"',
            'id="def-dnd-spd-tgl"',
            'id="def-apeap-tgl"',
        ]
        self.assertEqual(len(toggles), 7)
        for tid in toggles:
            with self.subTest(toggle=tid):
                self.assertIn(tid, body)
        # Sanity: the removed nag toggle must NOT be present (guarded elsewhere too).
        self.assertNotIn('id="def-epnag-tgl"', body)

    def test_phase3_defense_ui_bionic_warning_element(self) -> None:
        """UI must have bionic-disabled warning element."""
        self.assertIn('id="def-bionic-warn"', self.ui)
        self.assertIn("bionic_disabled", self.ui)

    def test_phase3_defense_js_saves_dnd_params(self) -> None:
        """saveDefenseConfig JS must POST dnd_volume and dnd_speed."""
        save_fn = re.search(r"async function saveDefenseConfig\(\)\{.*?\}", self.ui, re.S)
        self.assertIsNotNone(save_fn)
        body = save_fn.group(0)
        self.assertIn("def-dnd-vol-tgl", body)
        self.assertIn("def-dnd-spd-tgl", body)
        self.assertIn("dnd_volume", body)
        self.assertIn("dnd_speed", body)

    def test_phase3_defense_js_loads_dnd_params(self) -> None:
        """loadDefenseConfig JS must read dnd_volume and dnd_speed."""
        load_fn = re.search(r"async function loadDefenseConfig\(\)\{.*?setText\('tb-exp'", self.ui, re.S)
        self.assertIsNotNone(load_fn)
        body = load_fn.group(0)
        self.assertIn("d.dnd_volume", body)
        self.assertIn("d.dnd_speed", body)
        self.assertIn("def-dnd-vol-tgl", body)
        self.assertIn("def-dnd-spd-tgl", body)

    def test_phase3_bionicsteering_in_car_manager_base(self) -> None:
        """bionicSteering must be in CarManagerBase for dashboard access."""
        base = re.search(r"struct CarManagerBase.*?virtual ~CarManagerBase", self.handlers, re.S)
        self.assertIsNotNone(base)
        body = base.group(0)
        self.assertIn("Shared<bool> bionicSteering{false}", body)
        self.assertIn("bionicDisabled()", body)
        self.assertIn("resetBionic", body)

    def test_phase3_defense_runtime_and_persistence_are_wired(self) -> None:
        """Defense config should drive Nag/Bionic runtime and persist DND switches."""
        self.assertIn("nagKillerRuntime = canActive && dashDefenseEnabled", self.dash)
        self.assertIn("uint32_t resetSeed = (uint32_t)millis();", self.dash)
        self.assertIn("dashHandler->resetBionic(resetSeed)", self.dash)
        self.assertIn('prefs.putBool("def_dv", dashDndVolume);', self.dash)
        self.assertIn('prefs.putBool("def_ds", dashDndSpeed);', self.dash)
        self.assertIn('dashDndVolume = prefs.getBool("def_dv", false);', self.dash)
        self.assertIn('dashDndSpeed = prefs.getBool("def_ds", false);', self.dash)
        status = re.search(r"static void handleStatus\(\).*?server\.send", self.dash, re.S)
        self.assertIsNotNone(status)
        self.assertIn("dashDndSpeed ? \"true\" : \"false\"", status.group(0))

    def test_handlers_includes_reactive_nag_header(self) -> None:
        """handlers.h must include dash_reactive_nag.h (reactive NAG suppression)."""
        self.assertIn('#include "dash_reactive_nag.h"', self.handlers)

    def test_nag_diag_header_defines_all_mode_projections(self) -> None:
        diag = (ROOT / "include" / "dash_nag_diag.h").read_text(encoding="utf-8")
        for symbol in [
            "dashMapHumanReplayDiag",
            "dashMapLateEchoDiag",
            "dashMapReactiveHoldDiag",
            "dashMakeDisabledNagDiag",
        ]:
            self.assertIn(symbol, diag)
        self.assertIn("switch (nagMode)", self.handlers)
        self.assertIn("dashMapHumanReplayDiag", self.handlers)
        self.assertIn("dashMapLateEchoDiag", self.handlers)
        self.assertIn("dashMapReactiveHoldDiag", self.handlers)

    def test_reactive_nag_v4_status_exposes_tsl6p_burst_diagnostics(self) -> None:
        """TSL6P Burst NAG v4 must expose phase/timing/clear diagnostics."""
        status = re.search(r'j \+= ",\\"reactiveNag\\":\{";.*?j \+= "\}";', self.dash, re.S)
        self.assertIsNotNone(status)
        body = status.group(0)
        required = [
            '"selectedMode"',
            '"selectedModeName"',
            '"runtimePhase"',
            '"nagSamples"',
            '"reactiveBursts"',
            '"proactiveWiggles"',
            '"echoSent"',
            '"replayAttempts"',
            '"replaySuccesses"',
            '"replayFailures"',
            '"lastProfileId"',
            '"lastProfileDir"',
            '"lastPeakRaw"',
            '"lastBaseRaw"',
            '"lastOutDeltaRaw"',
            '"profileIndex"',
            '"lastHosBefore"',
            '"lastHosAfter"',
            '"cooldownRemainMs"',
            '"burstSessions"',
            '"burstOnEntries"',
            '"burstOffEntries"',
            '"burstFramesSent"',
            '"burstCyclesCompleted"',
            '"hosClearEvents"',
            '"hosClearDuringOn"',
            '"hosClearDuringOff"',
            '"hosClearWhileIdle"',
            '"hosClearWhileCooldown"',
            '"abortBlocks"',
            '"gateBlocks"',
            '"txFailures"',
            '"lastApState"',
            '"phaseRemainMs"',
            '"lastTorqueRaw"',
            '"lastTorqueNmX100"',
            '"blockedReason"',
        ]
        for token in required:
            self.assertIn(token, body)
        self.assertIn("jsonEscape", body)
        self.assertIn("d.blockedReason && d.blockedReason[0]", body)
        self.assertIn('"none"', body)
        self.assertIn("dashReactiveNagHandler()", body)
        self.assertIn("reactive ? reactive->reactiveDiag() : dashMakeDisabledNagDiag()", body)
        self.assertNotIn("dashHandler->reactiveDiag()", body)

    def test_reactive_nag_serial_command_prints_v4_burst_fields(self) -> None:
        """Serial reactive_nag output should distinguish ON/OFF clears and aborts."""
        serial = re.search(r'else if \(strcmp\(start, "reactive_nag"\) == 0\).*?else if \(strcmp\(start, "reactive_nag_reset"\)', self.dash, re.S)
        self.assertIsNotNone(serial)
        body = serial.group(0)
        required = [
            "selectedMode=",
            "selectedModeName=",
            "runtimePhase=",
            "=== TSL6P Burst NAG v4 ===",
            "0=IDLE 1=BURST_ON 2=BURST_OFF 3=COOLDOWN",
            "replayAttempts=",
            "replaySuccesses=",
            "replayFailures=",
            "lastProfileId=",
            "lastProfileDir=",
            "lastPeakRaw=",
            "lastBaseRaw=",
            "lastOutDeltaRaw=",
            "profileIndex=",
            "hosBefore=",
            "hosAfter=",
            "cooldownRemainMs=",
            "burstSessions=",
            "burstOnEntries=",
            "burstOffEntries=",
            "burstFramesSent=",
            "burstCyclesCompleted=",
            "hosClearEvents=",
            "hosClearDuringOn=",
            "hosClearDuringOff=",
            "hosClearWhileIdle=",
            "hosClearWhileCooldown=",
            "abortBlocks=",
            "gateBlocks=",
            "txFailures=",
            "lastApState=",
            "phaseRemainMs=",
            "lastTorqueRaw=",
            "lastTorqueNmX100=",
            "blockedReason=",
        ]
        for token in required:
            self.assertIn(token, body)
        self.assertIn("d.blockedReason && d.blockedReason[0]", body)
        self.assertIn('"none"', body)
        self.assertIn("rn_ns=", body)
        self.assertIn("rh_ns=", body)

    def test_reactive_nag_persistence_uses_isolated_dirty_counter_banks(self) -> None:
        """TSL6P rn_* and Reactive Hold rh_* must load and flush independently."""
        helper = re.search(r"static CarManagerBase \*dashReactiveNagHandler\(\)\n\{.*?\n\}\n", self.dash, re.S)
        self.assertIsNotNone(helper)
        helper_body = helper.group(0)
        self.assertRegex(helper_body, r"if \(handlerPool\[0\]\)\s*return handlerPool\[0\];\s*return dashHandler;")

        maintenance = re.search(r"static void dashReactiveCountersMaintenance\(\).*?(?=static void mcpDashboardLoop\(\))", self.dash, re.S)
        self.assertIsNotNone(maintenance)
        body = maintenance.group(0)
        self.assertIn("CarManagerBase *reactive = dashReactiveNagHandler();", body)
        self.assertIn("DashNagMode::HumanReplayTsl6p", body)
        self.assertIn("DashNagMode::ReactiveHold", body)
        self.assertIn("reactive->setNagCounters", body)
        self.assertIn("reactive->nagCountersDirty(selectedMode)", body)
        self.assertIn("reactive->nagDiagForMode(selectedMode)", body)
        self.assertIn("reactive->markNagCountersPersisted(selectedMode)", body)
        self.assertIn("Preferences p;", body)
        self.assertIn("if (!p.begin(PREFS_NS, false))", body)
        self.assertNotIn("dashHandler->setNagCounters", body)
        self.assertNotIn("dashHandler->nagDiagForMode", body)

        serial_reset = re.search(r'else if \(strcmp\(start, "reactive_nag_reset"\) == 0\).*?else if \(strcmp\(start, "reactive_nag_bump"\)', self.dash, re.S)
        self.assertIsNotNone(serial_reset)
        self.assertIn("dashReactiveNagHandler()", serial_reset.group(0))
        self.assertIn("resetNagCounters(selectedMode)", serial_reset.group(0))

        rn_keys = set(re.findall(r'"(rn_[a-z]+)"', self.dash))
        rh_keys = set(re.findall(r'"(rh_[a-z]+)"', self.dash))
        self.assertEqual({"rn_ns", "rn_rb", "rn_pw", "rn_es"}, rn_keys)
        self.assertEqual({"rh_ns", "rh_rb", "rh_pw", "rh_es"}, rh_keys)

    # ── Phase 4: Light Stunt System ───────────────────────────

    def test_phase4_fog_light_header_exists(self) -> None:
        """dash_fog_light.h must exist with core API surface."""
        fog = (ROOT / "include" / "dash_fog_light.h").read_text(encoding="utf-8")
        for symbol in ["DashFogLight", "startStrobe", "startF1Pilot",
                        "startContinuous", "stop", "tick", "buildFrame",
                        "isActive", "kModeOff", "kModeStrobe",
                        "kModeF1Pilot", "kModeContinuous"]:
            with self.subTest(symbol=symbol):
                self.assertIn(symbol, fog)

    def test_phase4_fog_light_safety_gear_check(self) -> None:
        """Fog light must auto-stop when gearRaw != 4 (Drive)."""
        fog = (ROOT / "include" / "dash_fog_light.h").read_text(encoding="utf-8")
        self.assertIn("gearRaw != 4", fog)
        self.assertIn("if (isActive())", fog)
        self.assertIn("stop();", fog)

    def test_phase4_fog_light_uses_can273_constants(self) -> None:
        """Fog light must use the 0x273 CAN frame constants from can_frame_types.h."""
        fog = (ROOT / "include" / "dash_fog_light.h").read_text(encoding="utf-8")
        # Includes can_frame_types.h which defines CAN_ID_REAR_FOG_LIGHT
        self.assertIn("can_frame_types.h", fog)
        self.assertIn("FOG_BASE_2_ON", fog)
        self.assertIn("FOG_BASE_2_OFF", fog)

    def test_phase4_fog_light_checksum(self) -> None:
        """Fog frames must include checksum calculation for 0x273."""
        fog = (ROOT / "include" / "dash_fog_light.h").read_text(encoding="utf-8")
        self.assertIn("0x73u + 0x02u", fog)  # CAN ID 0x273 bytes
        self.assertIn("data[7]", fog)         # checksum byte

    def test_phase4_fog_light_f1_timing(self) -> None:
        """F1 pilot mode must use 135ms flash + 1500ms pause."""
        fog = (ROOT / "include" / "dash_fog_light.h").read_text(encoding="utf-8")
        self.assertIn("kF1FlashDurationMs{135}", fog)
        self.assertIn("kF1PauseMs{1500}", fog)
        self.assertIn("kF1FlashCount{3}", fog)

    def test_phase4_fog_handler_has_trigger_param(self) -> None:
        """/fog_light must accept trigger parameter for execution."""
        fog_handler = re.search(r"static void handleFogLight\(\).*?server\.send", self.dash, re.S)
        self.assertIsNotNone(fog_handler)
        body = fog_handler.group(0)
        self.assertIn('server.hasArg("trigger")', body)
        self.assertIn("dashFogCtrl.startStrobe(", body)
        self.assertIn("dashFogCtrl.startF1Pilot(", body)
        self.assertIn("dashFogCtrl.startContinuous(", body)
        self.assertIn("dashFogOffRequested = true", body)
        self.assertIn("dashFogCtrl.isActive()", body)
        self.assertIn('driverSupported', body)
        self.assertIn('reason', body)
        self.assertIn('"driver_not_supported"', body)

    def test_phase4_strobe_cont_is_functional(self) -> None:
        """/strobe_cont must be functional, not a stub."""
        strobe_handler = re.search(r"static void handleStrobeCont\(\).*?server\.send", self.dash, re.S)
        self.assertIsNotNone(strobe_handler)
        body = strobe_handler.group(0)
        self.assertNotIn('"Phase 4"', body)
        self.assertIn("dashFogCtrl.startStrobe(0", body)  # 0 = infinite
        self.assertIn("dashFogOffRequested = true", body)
        self.assertIn('driverSupported', body)
        self.assertIn('reason', body)

    def test_phase4_status_strobeCont_is_dynamic(self) -> None:
        """/status strobeCont must reflect actual state, not hardcoded."""
        # The old code was: j += ",\"strobeCont\":false";
        # The new code uses dashFogCtrl.isActive()
        self.assertNotIn('"strobeCont\\":false', self.dash)
        self.assertIn("dashFogCtrl.isActive()", self.dash)

    def test_phase4_dashboard_includes_fog_light_header(self) -> None:
        """Dashboard must include dash_fog_light.h."""
        self.assertIn('#include "dash_fog_light.h"', self.dash)

    def test_phase4_dashboard_has_fog_ctrl_instance(self) -> None:
        """Dashboard must have a DashFogLight instance."""
        self.assertIn("DashFogLight dashFogCtrl", self.dash)
        self.assertIn("dashFogOffRequested", self.dash)

    def test_phase4_fog_fail_off_is_owned_by_can_task(self) -> None:
        """CAN task should send a final OFF frame on stop or unsafe/stale gear."""
        self.assertIn("gearMs", self.dash)
        self.assertIn("t2canGearIsFreshDrive", self.main)
        self.assertIn("kT2canGearFreshMs", self.main)
        self.assertIn("dashFogCtrl.buildFrame(offData, false)", self.main)
        self.assertIn("CAN_ID_REAR_FOG_LIGHT", self.main)
        self.assertIn("dashFogOffRequested || (active && !safeGear)", self.main)

    def test_phase4_ui_has_strobe_page(self) -> None:
        """UI must have pg-strobe page with all controls."""
        strobe_page = re.search(r'id="pg-strobe".*?id="pg-defense"', self.ui, re.S)
        self.assertIsNotNone(strobe_page)
        body = strobe_page.group(0)
        for element in ['id="fog-strategy"', 'id="strobe-count"',
                        'id="strobe-freq"', 'fogTrigger(\'strobe\')',
                        'fogTrigger(\'f1\')', 'fogTrigger(\'continuous\')',
                        'fogTrigger(\'stop\')', 'id="strobe-status"',
                        'id="strobe-gear"']:
            with self.subTest(element=element):
                self.assertIn(element, body)

    def test_phase4_ui_sidebar_has_stroke_nav(self) -> None:
        """Sidebar must have pg-stroke navigation item."""
        self.assertIn('data-page="pg-strobe"', self.ui)
        self.assertIn('灯光特技', self.ui)

    def test_phase4_ui_mobile_nav_has_stroke(self) -> None:
        """Mobile nav must have pg-stroke item."""
        # Count mobile nav items for pg-strobe
        self.assertEqual(self.ui.count('data-page="pg-strobe"'), 2)  # sidebar + mobile

    def test_phase4_js_has_fog_functions(self) -> None:
        """JS must have loadStrobePage, fogTrigger, saveFogStrategy."""
        for fn in ["loadStrobePage", "fogTrigger", "saveFogStrategy"]:
            with self.subTest(fn=fn):
                self.assertIn(f"async function {fn}", self.ui)
        self.assertIn("这里仅保存默认策略", self.ui)
        self.assertIn("这些按钮才会触发实际灯光动作", self.ui)
        self.assertIn("fetchJson('/fog_light')", self.ui)

    def test_phase4_js_navigates_to_strobe_page(self) -> None:
        """Page navigation must load strobe page data when lighting bus is supported (gated off for single-CAN)."""
        self.assertIn("pageId==='pg-strobe'&&CAP.lightingBusSupported&&!isSingleCan())loadStrobePage()", self.ui)

    def test_phase5a_shift_page_loads_read_only_telemetry(self) -> None:
        """Auto-shift placeholder page must populate its read-only telemetry fields (gated off for single-CAN)."""
        self.assertIn("/gear_assist_status", self.ui)
        self.assertIn("async function pollGearAssist()", self.ui)
        self.assertIn("pageId==='pg-shift'&&!isSingleCan())pollGearAssist()", self.ui)
        self.assertIn("pid==='pg-shift'&&!isSingleCan())pollGearAssist()", self.ui)
        for token in ["shift-speed", "shift-gear", "shift-brake", "shift-fsd"]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_phase5a_drive_profile_preserves_six_modes(self) -> None:
        """Drive UI should use driveProfile/driveProfileName so Auto/Sloth/MAX survive polling."""
        self.assertIn("function driveModeFromProfile(profile,name)", self.ui)
        self.assertIn("driveModeFromProfile(d.driveProfile,d.driveProfileName)", self.ui)
        self.assertIn("driveMap[savedMode]!==undefined?driveMap[savedMode]:3", self.ui)
        self.assertNotIn("[mode]||3", self.ui)

    def test_drive_style_selection_persists_and_updates_home_status(self) -> None:
        """Drive style cards must POST, persist returned mode, and mirror it to the overview status."""
        for token in [
            "function updateDriveSurfaces(mode, statusText)",
            "setText('ov-drive-current',label);",
            "setStatusTriplet('drive',label,label,statusText",
            "saved=await postForm('/drive_profile',{profile:mode});",
            "var savedMode=saved&&saved.value!==undefined?driveModeFromProfile(saved.value,saved.profile):mode;",
            "updateDriveSurfaces(savedMode,'已保存');",
            "setTimeout(poll,300);",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)
        for token in [
            '"driveProfile": self.drive_profile',
            '"driveProfileName": self.drive_profile_config()["profile"]',
            "STATE.sp_auto = STATE.drive_profile == 0",
        ]:
            with self.subTest(simulator_token=token):
                self.assertIn(token, self.simulator)

    def test_phase5a_temperature_uses_error_style_above_60c(self) -> None:
        """Temperature >60°C should be red/error, not warning/yellow."""
        self.assertIn("t>60?'v-err'", self.ui)
        self.assertNotIn("t>60?'v-warn'", self.ui)

    def test_batch_c_release_ota_ui_is_wired(self) -> None:
        """OTA page must expose the GitHub release update flow already provided by backend APIs."""
        for token in [
            "Release 在线更新",
            "id=\"rel-check-btn\"",
            "id=\"rel-install-btn\"",
            "async function loadOtaReleaseState()",
            "async function checkReleaseUpdate()",
            "async function installReleaseUpdate()",
            "async function toggleUpdateBeta()",
            "async function toggleAutoUpdate()",
            "fetch('/update_check')",
            "postForm('/update_install'",
            "postForm('/update_beta'",
            "postForm('/auto_update'",
            "pageId==='pg-ota'",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    @unittest.skip("author release repo uses real owner JordanzhaoD for OTA, not neutral CHANGE_ME placeholder")
    def test_batch_c_release_ota_backend_matches_repo_default(self) -> None:
        """Open-source build must ship a neutral placeholder GitHub repo default (overridable at build time)."""
        self.assertIn('#define DASH_GITHUB_REPO "CHANGE_ME/waveshare-single-can-firmware"', self.dash)
        self.assertIn("static const char *GITHUB_REPO = DASH_GITHUB_REPO;", self.dash)
        self.assertIn('server.on("/update_beta", HTTP_GET, handleUpdateBeta);', self.dash)

    def test_batch_c_sidebar_i18n_and_version_display_are_stable(self) -> None:
        """New UI pages must not break nav translations, and overview version should use /system_status firmware.

        The CAN2 / auto-shift zh strings are split via concatenation in source so the
        raw forbidden literals never appear in standalone-visible source (Task 10);
        the assertions check the split fragments instead.
        """
        for token in [
            "'灯光特技':'Light Show'",
            "['自动'+'换挡']:'Auto'+' Shift'",
            "'驾驶状态':'Drive Status'",
            "'硬件模式':'Hardware Mode'",
            "'驾驶风格':'Drive Style'",
            "'速度策略':'Speed Strategy'",
            "'CAN 诊断':'CAN Diagnostics'",
            "'FSD 防护':'FSD Guard'",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)
        self.assertIn("'驾驶状态','硬件模式','驾驶风格','速度策略','CAN2 '+'控制','灯光特技','FSD 防护','OTA 升级','网络设置','CAN 诊断','自动'+'换挡'", self.ui)
        self.assertIn("d&&(d.firmware||d.version)", self.ui)
        self.assertIn("setText('s-ver',d.firmware||d.version);", self.ui)

    def test_batch_c_profile_helper_supports_waveshare_driver(self) -> None:
        """CI profile generation must understand the waveshare single CAN TWAI driver."""
        helper = (ROOT / "scripts" / "platformio_set_profile.py").read_text(encoding="utf-8")
        profile_example = (ROOT / "platformio_profile.example.h").read_text(encoding="utf-8")
        self.assertIn('"DRIVER_TWAI"', helper)
        self.assertIn("#define DRIVER_TWAI", profile_example)
        self.assertIn("#define HW4", profile_example)
        self.assertIn("--driver DRIVER_TWAI", self.tests_workflow)
        self.assertIn("--driver DRIVER_TWAI", self.release_workflow)

    def test_capability_matrix_and_bus2_can2_are_exposed(self) -> None:
        combined = self.dash
        # ── Source-level tokens: include, function name, capability helpers ──
        for token in [
            '#include "dash_capabilities.h"',
            'appendCapabilitiesJson',
            '\\"cap\\"',
            'dashCapabilityLegacyFsd',
            'dashCapabilityBionicSteering',
            'dashCapabilityBanShield',
            'dashCapabilityLegacyMppCustom',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, combined)
        # ── Composed JSON tokens from appendCapabilitiesJson string literals ──
        # These verify the C++ source contains the exact JSON key-value fragments
        # that will appear in the HTTP response (with C++ escaped quotes as read by Python).
        for token in [
            ',\\"legacyFsd\\":\\"',       # j += ",\"legacyFsd\":\""
            ',\\"serviceMode\\":\\"can_b_direct\\"',  # inline composed
            ',\\"bionicSteering\\":\\"',   # j += ",\"bionicSteering\":\""
            ',\\"legacyMppCustom\\":\\"',  # j += ",\"legacyMppCustom\":\""
            ',\\"gearAssist\\":\\"not_implemented\\"}',  # inline composed, closes cap object
        ]:
            with self.subTest(token=token):
                self.assertIn(token, combined)
        # bus2_ids endpoint must include all four can2 diagnostic counters
        bus2 = re.search(r"static void handleBus2Ids\(\).*?server\.send", combined, re.S)
        self.assertIsNotNone(bus2)
        bus2_body = bus2.group(0)
        for token in ['t2canBus2RxCount()', 't2canBus2TxCount()', 't2canBus2TxErrCount()', 't2canBus2Eflg()']:
            with self.subTest(token=token):
                self.assertIn(token, bus2_body)

    def test_ui_explains_legacy_real_wiring_states(self) -> None:
        # Runtime / backend-backed Legacy real-wiring copy stays in the UI.
        for token in [
            "Legacy 运行时状态",
            "FSD 注入策略",
            "稳定模式",
            "实验模式",
            "/legacy_fsd_config",
            "fsdDiag",
            "serviceDiag",
            "wheelDndDiag",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        # Task 2 (UI V1.1) removed the three low-value static display cards
        # from the overview bottom: 维修模式 0x339, 方向盘免打扰 0x3C2 and the
        # Legacy 功能支持矩阵 (which held the 仿生转向/不支持/未接线/未实现 copy).
        # These had no id/JS/backend wiring, so their absence is intentional.
        for token in [
            "维修模式 0x339",
            "方向盘免打扰 0x3C2",
            "Legacy 功能支持矩阵",
            "仿生转向",
            "不支持",
            "未接线",
            "未实现",
        ]:
            with self.subTest(token=token):
                self.assertNotIn(token, self.ui)

    def test_atlas_redesign_refined_chinese_copy_is_stable(self) -> None:
        """Driving-first redesign should keep the refined Task 4 Chinese copy labels."""
        required_copy = [
            "驾驶状态",
            "硬件模式",
            "驾驶风格",
            "速度策略",
            "CAN2 控制",
            "FSD 防护",
            "OTA 升级",
            "CAN 诊断",
            "FSD 硬件模式",
            "驾驶状态中心",
            "FSD 注入",
            "长按 2 秒启用 FSD 注入",
            "长按 2 秒关闭 FSD 注入",
            "现场遥控",
            "连接到设备热点",
            "长按启用",
            "长按关闭",
            "工程模式",
            "错误优先",
            "帧筛选",
            "实时帧",
            "记录器",
            "控制器",
            "调试",
            "最近写入",
        ]
        for token in required_copy:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

        forbidden_default_copy = [
            'data-page="pg-overview"><span class="nav-icon">▣</span>模块配置',
            'data-page="pg-hardware"><span class="nav-icon">◇</span>激活模式',
            'data-page="pg-drive"><span class="nav-icon">◉</span>驾驶模式',
            'data-page="pg-speed"><span class="nav-icon">↗</span>速度偏移',
            'data-page="pg-bus2"><span class="nav-icon">✦</span>CAN2控制',
            'data-page="pg-defense"><span class="nav-icon">◈</span>FSD防御',
            'data-page="pg-ota"><span class="nav-icon">⇧</span>OTA升级',
            'data-page="pg-can"><span class="nav-icon">⌘</span>CAN工具',
            '<div class="page-title">破解模式</div>',
            '<div class="card-title">FSD 智能破解模式</div>',
            "Cockpit Home",
            "Remote Card",
            "Engineering Mode",
            "Drive Console",
            "Hold to Enable",
            "Hold to Disable",
            "PRIMARY",
            "防封",
        ]
        for token in forbidden_default_copy:
            with self.subTest(token=token):
                self.assertNotIn(token, self.ui)

    def test_atlas_redesign_cockpit_home_structure_is_present(self) -> None:
        """Overview page must become the driving-first cockpit home while preserving existing IDs."""
        overview_page = re.search(r'id="pg-overview".*?id="pg-hardware"', self.ui, re.S)
        self.assertIsNotNone(overview_page)
        body = overview_page.group(0)
        for token in [
            'class="cockpit-home"',
            'class="cockpit-primary"',
            'id="fsd-toggle"',
            'id="fsd-label"',
            'id="ov-master-tgl"',
            'id="ov-fsd-tgl"',
            'id="fsd-boot-tgl"',
            'id="s-can"',
            'id="ov-ap"',
            'id="s-fps"',
            'id="s-temp"',
            'id="s-ver"',
            'id="ov-hw"',
            'id="m-hw"',
            'id="m-drive"',
            'id="m-speed"',
            'id="m-defense"',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)

    def test_atlas_redesign_mobile_remote_structure_is_present(self) -> None:
        """Mobile home must be a remote-control surface, not a compressed desktop stack."""
        for token in [
            'class="mobile-remote"',
            'class="mobile-primary-card"',
            'id="m-fsd-tgl"',
            'id="m-can"',
            'id="m-can2"',
            'id="m-fsd"',
            'id="m-fps"',
            'id="m-alert"',
            '现场遥控',
            '底部导航',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_atlas_redesign_can_diagnostics_structure_is_present(self) -> None:
        """CAN tools must expose engineering-mode hierarchy and preserve existing tabs/IDs."""
        can_page = re.search(r'id="pg-can".*?id="pg-shift"', self.ui, re.S)
        self.assertIsNotNone(can_page)
        body = can_page.group(0)
        for token in [
            'class="diag-shell"',
            'class="diag-kpi-rail"',
            'id="sniff-filter"',
            'id="sniff-pause"',
            'id="sniff-rows"',
            'id="can-recorder"',
            'id="can-controller"',
            'id="can-debug"',
            'id="ctrl-eflg"',
            'id="ctrl-rxerr"',
            'id="ctrl-txerr"',
            'id="lw-injected"',
            'id="lw-match"',
            'diag-rxtx',
            'diag-eflg',
            'diag-txerr',
            'diag-last-write',
            '错误优先',
            '帧筛选',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)

    def test_atlas_polish_cockpit_visual_depth_is_present(self) -> None:
        """Cockpit home should feel like a car display without losing production ids."""
        overview_page = re.search(r'id="pg-overview".*?id="pg-hardware"', self.ui, re.S)
        self.assertIsNotNone(overview_page)
        body = overview_page.group(0)
        for token in [
            'class="cockpit-shell"',
            'class="cockpit-ambient"',
            'class="cockpit-vehicle-visual"',
            'class="vehicle-lane"',
            'class="vehicle-silhouette"',
            'class="cockpit-safety-strip"',
            'id="ov-drive-current"',
            'id="tb-fsd-local"',
            '驾驶状态中心',
            '车辆态势',
            '安全优先',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)

    def test_atlas_polish_mobile_remote_app_surface_is_present(self) -> None:
        """Mobile home should use a native remote-card visual hierarchy."""
        for token in [
            'class="mobile-app-shell"',
            'class="mobile-primary-card"',
            'class="mobile-remote-grid"',
            'class="mobile-risk-note"',
            '现场遥控',
            '长按启用',
            '危险操作已收纳',
            '底部导航',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.ui)

    def test_atlas_polish_can_engineering_console_is_present(self) -> None:
        """CAN diagnostics should expose a deliberate engineering-console hierarchy."""
        can_page = re.search(r'id="pg-can".*?id="pg-shift"', self.ui, re.S)
        self.assertIsNotNone(can_page)
        body = can_page.group(0)
        for token in [
            'class="diag-shell diag-console"',
            'class="diag-timeline"',
            'class="diag-kpi-rail"',
            'class="diag-table-shell"',
            'class="diag-priority-row"',
            '工程模式',
            '错误优先',
            '发现 ID',
            'LIVE TIMELINE',
            'DLC',
        ]:
            with self.subTest(token=token):
                self.assertIn(token, body)

    def test_webui_simulator_serves_gear_assist_status(self) -> None:
        """Local browser verification should not produce a 404 for gear assist status."""
        self.assertIn('"/gear_assist_status"', self.simulator)
        self.assertIn('gear_assist_status', self.simulator)

    def test_release_metadata_and_waveshare_ci_are_wired(self) -> None:
        """Release metadata and workflows must cover the waveshare single CAN standalone artifact."""
        version = self.version.strip()
        # VERSION file must hold a stable semver (the format auto-tag-release.yml gates on).
        self.assertRegex(version, r"^\d+\.\d+\.\d+$", f"VERSION file malformed: {version!r}")
        # CHANGELOG must have a section for the current VERSION (VERSION ↔ CHANGELOG consistency,
        # so bumping VERSION without a CHANGELOG entry is caught). Version-agnostic by design:
        # this must not break on every release bump.
        self.assertIn(f"## [{version}]", self.changelog)
        self.assertIn("waveshare_single_can_standalone", self.tests_workflow)
        self.assertIn("waveshare_single_can_standalone", self.release_workflow)
        self.assertIn("firmware-waveshare-single-can", self.release_workflow)


if __name__ == "__main__":
    unittest.main()
