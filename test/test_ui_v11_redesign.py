import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class UiV11RedesignTests(unittest.TestCase):
    def assert_present(self, needle: str) -> None:
        self.assertIn(needle, SRC, f"UI 源缺少: {needle!r}")

    def assert_absent(self, needle: str) -> None:
        self.assertNotIn(needle, SRC, f"UI 源不应再含: {needle!r}")

    def assert_id_present(self, element_id: str) -> None:
        pattern = rf'\bid="{re.escape(element_id)}"'
        self.assertRegex(SRC, pattern, f"缺少 id={element_id}")


class DesignSystemTests(UiV11RedesignTests):
    def test_ds_tokens_defined(self):
        for tok in ["--ds-cyan", "--ds-indigo", "--ds-accent-grad"]:
            self.assert_present(tok + ":")

    def test_card_is_cockpit_glass(self):
        # .card 重定义为驾驶舱玻璃风：深色 + backdrop-filter
        self.assertRegex(SRC, r"\.card\s*\{[^}]*backdrop-filter")
        self.assert_present("--ds-accent-grad")


class RemoveBottomBlocksTests(UiV11RedesignTests):
    def test_removed_titles_absent(self):
        for t in ["维修模式 0x339", "方向盘免打扰 0x3C2", "Legacy 功能支持矩阵"]:
            self.assert_absent(t)

    def test_retained_neighbors_present(self):
        # 相邻保留块
        self.assert_present("Legacy CAN-A 诊断")
        self.assert_present("AP 注入安全")


class Can2ToApTests(UiV11RedesignTests):
    def test_ap_metric_replaces_can2(self):
        self.assert_id_present("ov-ap")          # 新 AP 激活格
        self.assert_present("AP 激活")
        # 原 CAN2 metric 的 id 不应再作为 CAN2 指标出现
        self.assert_absent('<div class="lbl">CAN2</div><div class="val" id="s-can2">')

    def test_ap_update_js_present(self):
        self.assert_present("setText('ov-ap'")


class ApInjectionRedesignTests(UiV11RedesignTests):
    def test_ap_card_cockpit_structure(self):
        self.assert_present("🛡")                    # 卡头图标
        self.assert_present("AP 注入安全")
        self.assert_id_present("ap-state-panel")     # 状态主区
        self.assert_id_present("ap-gate-bar")        # Gate 进度条

    def test_controls_chinese(self):
        self.assert_present("AP 门控")
        self.assert_present("延迟注入")
        self.assert_present("AP 自动恢复")

    def test_failclosed_note_present(self):
        self.assert_present("Fail-closed")           # 安全条

    def test_original_ids_preserved(self):
        # 硬要求：原 id 全保留（review 会查）
        for i in ["ap-core-card", "ap-core-state-pill", "ap-core-gate-tgl",
                  "ap-delay-select", "ap-auto-restore-tgl", "injection-source",
                  "ap-core-state-detail"]:
            self.assert_id_present(i)


class PluginRedesignTests(UiV11RedesignTests):
    def test_plugin_card_cockpit(self):
        self.assert_present("🧩")
        self.assert_present("插件管理")
        self.assert_id_present("plugin-url")
        self.assert_id_present("plugin-file")
        self.assert_id_present("plugin-json")
        self.assert_id_present("plugin-list")
        self.assert_id_present("plugin-replay-count")

    def test_no_english_install_labels(self):
        # 中文化：英文按钮文案应移除
        self.assert_absent("Official JSON plugin manager")
        self.assert_absent("Install from URL")

    def test_original_ids_preserved(self):
        for i in ["plugins-card","plugin-editor-json","plugin-rule-test-result"]:
            self.assert_id_present(i)

    def test_plugin_upload_has_explicit_button_and_feedback(self):
        # 修复：.json 上传必须有显式按钮（与 URL/JSON 行一致），不能仅靠 onchange 静默自动上传
        # 死装饰 span（看着像按钮但无 onclick）必须移除
        self.assert_absent('ds-btn-muted">选择 .json')
        self.assertRegex(
            SRC,
            r'<button[^>]*onclick="uploadPluginJson\(\)"[^>]*>[^<]*上传</button>',
            "上传文件行缺少显式\"上传\"按钮",
        )
        # 上传状态反馈元素（请先选择/上传中/成功/失败）
        self.assert_id_present("plugin-upload-status")
        self.assert_present("上传中")
        self.assert_present("安装失败")
        # 必须解析并显示后端 {ok,message} 的真实原因，不能只显示 HTTP 状态码
        self.assert_present("j.message")
        self.assert_present("JSON.parse")


class MobileNavTests(UiV11RedesignTests):
    def _standalone_tabs_block(self) -> str:
        match = re.search(r'<div class="mob-tabs" id="mob-tabs"(?:\s+[^>]*)?>[\s\S]*?</div>\s*<div class="mob-more-panel" id="mob-more-single"', SRC)
        self.assertIsNotNone(match, "缺少 #mob-tabs 单 CAN 导航块")
        return match.group(0) if match else ""

    def _standalone_more_block(self) -> str:
        match = re.search(r'<div class="mob-more-panel" id="mob-more-single"[\s\S]*?</div>\s*</div>\s*<!-- Mobile Dual-CAN Nav', SRC)
        self.assertIsNotNone(match, "缺少 #mob-more-single 单 CAN 更多面板")
        return match.group(0) if match else ""

    def _function_body(self, name: str) -> str:
        match = re.search(rf"function {re.escape(name)}\([^)]*\)\{{[\s\S]*?\n\}}", SRC)
        self.assertIsNotNone(match, f"缺少函数 {name}")
        return match.group(0) if match else ""

    def _show_mobile_map(self) -> str:
        show_mobile = re.search(r"function showMobilePage\(name\)\{[\s\S]*?var map=\{([^}]*)\}", SRC)
        self.assertIsNotNone(show_mobile, "缺少 showMobilePage map")
        return show_mobile.group(1) if show_mobile else ""

    def test_single_can_mobile_nav_has_exactly_six_ordered_tabs(self):
        tabs = re.findall(
            r'<button class="mob-tab(?: active)?" data-mobile-page="([^"]+)" onclick="([^"]+)">\s*<div class="mob-icon">[^<]*</div><div>([^<]+)</div></button>',
            self._standalone_tabs_block(),
        )
        self.assertEqual(
            [("driving", "showMobilePage('driving')", "驾驶"),
             ("hardware", "showMobilePage('hardware')", "硬件"),
             ("speed", "showMobilePage('speed')", "速度"),
             ("network", "showMobilePage('network')", "网络"),
             ("defense", "showMobilePage('defense')", "防护"),
             ("more", "toggleMobMore('mob-more-single')", "更多")],
            tabs,
            "#mob-tabs 必须只有 6 个单 CAN tab（含防护），且顺序/标签/入口固定",
        )
        self.assert_absent('data-mobile-page="home"')
        self.assert_absent('data-mobile-page="plugins"')

    def test_show_mobile_page_map_routes_standalone_tabs_to_pg_pages(self):
        show_mobile_map = self._show_mobile_map()
        for mobile_name, page_id in [
            ("driving", "pg-overview"),
            ("hardware", "pg-hardware"),
            ("speed", "pg-speed"),
            ("network", "pg-network"),
            ("drivingstyle", "pg-drive"),
        ]:
            self.assertRegex(show_mobile_map, rf"{mobile_name}\s*:\s*'{page_id}'")

    def test_dom_content_loaded_initializes_standalone_driving_page(self):
        dom_ready = re.search(r"document\.addEventListener\('DOMContentLoaded',function\(\)\{[\s\S]*?\n\}\);", SRC)
        self.assertIsNotNone(dom_ready, "缺少 DOMContentLoaded 初始化")
        self.assertIn("showMobilePage('driving');", dom_ready.group(0) if dom_ready else "")

    def test_standalone_direct_pages_do_not_activate_legacy_mobile_overlays(self):
        body = self._function_body("showMobilePage")
        self.assertNotIn("data-mobile-page-panel", body)
        self.assertNotIn("panels[i].classList.toggle('active'", body)
        for legacy_panel in [
            'data-mobile-page-panel="driving"',
            'data-mobile-page-panel="hardware"',
            'data-mobile-page-panel="speed"',
            'data-mobile-page-panel="network"',
        ]:
            self.assert_absent(legacy_panel)

    def test_single_can_more_panel_contains_only_task6_entries_with_onclick_routes(self):
        panel = self._standalone_more_block()
        expected = [
            ("drivingstyle", "驾驶风格"),
            ("defense", "FSD 防护"),
            ("ota", "OTA 升级"),
            ("can", "CAN 诊断"),
        ]
        items = re.findall(r'<div class="mob-more-item" onclick="showStandaloneMorePage\(\'([^\']+)\'\)">[^<]*?([^<]+)</div>', panel)
        normalized = [(name, re.sub(r"^[^\w\u4e00-\u9fff]+\s*", "", label.strip())) for name, label in items]
        self.assertEqual(expected, normalized)

        show_mobile_map = self._show_mobile_map()
        for mobile_name, page_id in [
            ("drivingstyle", "pg-drive"),
            ("defense", "pg-defense"),
            ("ota", "pg-ota"),
            ("can", "pg-can"),
        ]:
            self.assertRegex(show_mobile_map, rf"{mobile_name}\s*:\s*'{page_id}'")

        for label in ["CAN2 控制", "灯光特技", "自动换挡"]:
            self.assertNotIn(label, panel, f"单 CAN 更多面板不应包含 {label}")

    def test_dual_can_mobile_nav_is_preserved(self):
        self.assert_present('id="mob-tabs-dual"')
        self.assert_present('id="mob-more" data-single-hide="1"')
        for page in ["pg-bus2", "pg-strobe", "pg-shift"]:
            self.assertRegex(SRC, rf'id="mob-more"[\s\S]*data-page="{page}"')


class DrivePageCockpitTests(UiV11RedesignTests):
    """Task 7：驾驶风格页套驾驶舱基底（功能零增减，只换皮）。"""

    def _drive_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-drive">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-drive 页")
        return m.group(1)

    def test_drive_page_keeps_core_labels_and_grid(self):
        self.assert_present("驾驶风格")
        self.assert_present('class="drive-grid"')

    def test_drive_card_uses_cockpit_baseline(self):
        page = self._drive_page()
        self.assertIn("ck-head", page, "驾驶风格页缺少 ck-head")
        self.assertIn("cockpit-card", page, "驾驶风格页缺少 cockpit-card")
        self.assertIn("◉", page, "驾驶风格页 ck-head 缺少 ◉ 图标")

    def test_drive_head_has_live_status_chip(self):
        self.assert_id_present("st-drive-pill")
        self.assert_present("setText('st-drive-pill'")

    def test_drive_page_drops_legacy_page_title(self):
        page = self._drive_page()
        self.assertNotIn('<div class="page-title">驾驶风格</div>', page,
                         "page-title 应已换皮为 ck-head")

    def test_drive_card_active_uses_accent_palette(self):
        # 旧紫色 active 规则退役，改用驾驶舱 cyan/indigo 色系（--ds-accent-grad 两端）
        m = re.search(r"\.drive-card\.active\s*\{([^}]*)\}", SRC)
        self.assertIsNotNone(m, "缺少 .drive-card.active 规则")
        body = m.group(1)
        self.assertNotIn("124,58,237", body, "drive-card active 仍用旧紫色")
        self.assertIn("14,165,233", body, "drive-card active 未用 cyan 端配色")

    def test_drive_modes_and_ids_preserved(self):
        # 功能零增减：6 档 onclick + 关键 id 全保留
        for mode in ["setDriveMode('auto')", "setDriveMode('sloth')", "setDriveMode('chill')",
                     "setDriveMode('normal')", "setDriveMode('hurry')", "setDriveMode('max')"]:
            self.assert_present(mode)
        for i in ["drive-cards", "drive-current",
                  "st-drive-ui", "st-drive-nvs", "st-drive-run"]:
            self.assert_id_present(i)


class SpeedPageCockpitTests(UiV11RedesignTests):
    """Task 8：速度策略页套驾驶舱基底（功能零增减，只换皮）。"""

    def _speed_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-speed">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-speed 页")
        return m.group(1)

    def test_speed_page_keeps_core_label(self):
        self.assert_present("速度策略")

    def test_speed_card_uses_cockpit_baseline(self):
        page = self._speed_page()
        self.assertIn("ck-head", page, "速度策略页缺少 ck-head")
        self.assertIn("cockpit-card", page, "速度策略页缺少 cockpit-card")
        self.assertIn("◉", page, "速度策略页 ck-head 缺少 ◉ 图标")

    def test_speed_head_has_live_status_chip(self):
        self.assert_id_present("st-speed-pill")
        self.assert_present("setText('st-speed-pill'")

    def test_speed_page_drops_legacy_page_title(self):
        page = self._speed_page()
        self.assertNotIn('<div class="page-title">速度策略</div>', page,
                         "page-title 应已换皮为 ck-head")

    def test_speed_ids_and_panels_preserved(self):
        # 功能零增减：模式 tabs、三面板、4 区间输入、编码、三态全保留
        for i in ["speed-mode-tabs", "speed-current",
                  "speed-panel-fixed", "speed-panel-auto", "speed-panel-custom",
                  "speed-cp1", "speed-cp2", "speed-cp3", "speed-cp4",
                  "hw3-enc", "st-speed-ui", "st-speed-nvs", "st-speed-run"]:
            self.assert_id_present(i)


class DefensePageCockpitTests(UiV11RedesignTests):
    """Task 9：FSD 防护页套驾驶舱基底（功能零增减，只换皮）。"""

    def _defense_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-defense">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-defense 页")
        return m.group(1)

    def test_defense_page_keeps_core_label_and_caveat(self):
        self.assert_present("FSD 防护")
        self.assert_present("部分实验")  # 实验性警示保留

    def test_defense_card_uses_cockpit_baseline(self):
        page = self._defense_page()
        self.assertIn("ck-head", page, "FSD 防护页缺少 ck-head")
        self.assertIn("cockpit-card", page, "FSD 防护页缺少 cockpit-card")
        self.assertIn("◉", page, "FSD 防护页 ck-head 缺少 ◉ 图标")

    def test_defense_page_drops_legacy_page_title(self):
        page = self._defense_page()
        self.assertNotIn('<div class="page-title">FSD 防护', page,
                         "page-title 应已换皮为 ck-head")

    def test_defense_toggles_and_ids_preserved(self):
        # NAG 改为独立模式卡；其余 6 个防护开关与状态保留。
        # (Was 8 toggles; EPAS-nag toggle def-epnag-tgl removed in the
        # nag-injection safety takedown — see test_no_epas_nag_contract.)
        for i in ["hw3-slew-tgl", "def-sound-tgl",
                  "def-dnd-vol-tgl", "def-speed-nd-tgl", "def-dnd-spd-tgl",
                  "def-apeap-tgl", "nag-mode-select",
                  "def-rate", "def-min", "def-cnt", "def-cur",
                  "def-dot", "def-status",
                  "st-defense-ui", "st-defense-nvs", "st-defense-run"]:
            self.assert_id_present(i)
        self.assertNotIn('id="def-bionic-tgl"', self._defense_page())


class OtaPageCockpitTests(UiV11RedesignTests):
    """Task 10：OTA 升级页套驾驶舱基底（功能零增减，只换皮）。"""

    def _ota_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-ota">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-ota 页")
        return m.group(1)

    def test_ota_page_keeps_core_label(self):
        self.assert_present("OTA 升级")

    def test_ota_card_uses_cockpit_baseline(self):
        page = self._ota_page()
        self.assertIn("ck-head", page, "OTA 升级页缺少 ck-head")
        self.assertIn("cockpit-card", page, "OTA 升级页缺少 cockpit-card")
        self.assertIn("◉", page, "OTA 升级页 ck-head 缺少 ◉ 图标")

    def test_ota_head_has_live_version_chip(self):
        self.assert_id_present("st-ota-pill")
        self.assert_present("setText('st-ota-pill'")

    def test_ota_page_drops_legacy_page_title(self):
        page = self._ota_page()
        self.assertNotIn('<div class="page-title">OTA 升级</div>', page,
                         "page-title 应已换皮为 ck-head")

    def test_ota_ids_preserved(self):
        # 功能零增减：固件信息 + Release 更新 + 上传全保留
        for i in ["ota-ver", "ota-build", "ota-flash", "ota-sdk",
                  "rel-current", "rel-latest", "rel-artifact", "rel-status",
                  "rel-beta-tgl", "rel-auto-tgl", "rel-check-btn", "rel-install-btn", "rel-msg",
                  "ota-drop", "ota-file", "ota-btn", "ota-reset-btn",
                  "ota-progress", "ota-bar", "ota-pct"]:
            self.assert_id_present(i)


class NetworkPageCockpitTests(UiV11RedesignTests):
    """Task 11：网络设置页套驾驶舱基底（功能零增减，只换皮；不破坏 wifi 回归）。"""

    def _network_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-network">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-network 页")
        return m.group(1)

    def test_network_page_keeps_core_label(self):
        self.assert_present("网络设置")

    def test_network_card_uses_cockpit_baseline(self):
        page = self._network_page()
        self.assertIn("ck-head", page, "网络设置页缺少 ck-head")
        self.assertIn("cockpit-card", page, "网络设置页缺少 cockpit-card")
        self.assertIn("◉", page, "网络设置页 ck-head 缺少 ◉ 图标")

    def test_network_head_has_live_status_chip(self):
        self.assert_id_present("st-net-pill")
        self.assert_present("setText('st-net-pill'")

    def test_network_page_drops_legacy_page_title(self):
        page = self._network_page()
        self.assertNotIn('<div class="page-title">网络设置</div>', page,
                         "page-title 应已换皮为 ck-head")

    def test_network_wifi_ids_preserved(self):
        # 功能零增减 + wifi 回归保护：所有 wifi/ap/gw/dns id 全保留
        for i in ["wifi-status", "wifi-slots", "wifi-form", "wf-ssid", "wf-pass",
                  "ap-ssid-input", "ap-pass-input", "ap-hidden-tgl",
                  "gw-nat-tgl", "gw-perf-tgl",
                  "dns-blacklist", "dns-whitelist",
                  "st-net-ui", "st-net-nvs", "st-net-run",
                  "st-dns-ui", "st-dns-nvs", "st-dns-run"]:
            self.assert_id_present(i)


class CanPageCockpitTests(UiV11RedesignTests):
    """Task 12：CAN 诊断页套驾驶舱基底（功能零增减，只换皮；保留 diag-shell 工程结构）。"""

    def _can_page(self) -> str:
        m = re.search(r'<div class="page" id="pg-can">([\s\S]*?)<div class="page" id="pg-', SRC)
        self.assertIsNotNone(m, "缺少 pg-can 页")
        return m.group(1)

    def test_can_page_keeps_core_label(self):
        self.assert_present("CAN 诊断")

    def test_can_card_uses_cockpit_baseline(self):
        page = self._can_page()
        self.assertIn("ck-head", page, "CAN 诊断页缺少 ck-head")
        self.assertIn("cockpit-card", page, "CAN 诊断页缺少 cockpit-card")
        self.assertIn("◉", page, "CAN 诊断页 ck-head 缺少 ◉ 图标")

    def test_can_head_has_live_rxtx_chip(self):
        self.assert_id_present("st-can-pill")
        self.assert_present("setText('st-can-pill'")

    def test_can_page_drops_legacy_page_title(self):
        page = self._can_page()
        self.assertNotIn('<div class="page-title">CAN 诊断</div>', page,
                         "page-title 应已换皮为 ck-head")

    def test_can_engineering_structure_preserved(self):
        # 功能零增减：diag-shell 工程结构 + 所有诊断 id 全保留
        page = self._can_page()
        for token in ['class="diag-shell diag-console"', 'class="diag-kpi-rail"',
                      'class="diag-table-shell"', '工程模式', '错误优先']:
            self.assertIn(token, page)
        for i in ["diag-rxtx", "diag-eflg", "diag-txerr", "diag-last-write", "diag-id-count",
                  "sniff-filter", "sniff-pause", "sniff-rows", "sniff-count",
                  "can-recorder", "can-controller", "can-debug",
                  "ctrl-eflg", "ctrl-rxerr", "ctrl-txerr",
                  "lw-injected", "lw-match", "debug-tgl",
                  "can-cs", "can-sck", "can-miso", "can-mosi", "can-rst"]:
            self.assert_id_present(i)


if __name__ == "__main__":
    unittest.main()
