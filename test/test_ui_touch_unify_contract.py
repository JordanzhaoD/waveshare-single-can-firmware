import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class TouchUnifyTests(unittest.TestCase):
    def assert_present(self, needle: str) -> None:
        self.assertIn(needle, SRC, f"UI 源缺少: {needle!r}")

    def assert_absent(self, needle: str) -> None:
        self.assertNotIn(needle, SRC, f"UI 源不应再含: {needle!r}")

    def assert_id_present(self, element_id: str) -> None:
        self.assertRegex(SRC, rf'\bid="{re.escape(element_id)}"',
                         f"缺少 id={element_id}")


class StepperPrimitiveTests(TouchUnifyTests):
    def test_stepper_css_defined(self):
        # .stepper / .stepper-btn / .stepper-val 视觉原语已定义
        for cls in [".stepper {", ".stepper-btn", ".stepper-val"]:
            self.assert_present(cls)

    def test_stepper_js_helpers_defined(self):
        self.assert_present("function initStepper(")
        self.assert_present("function stepStepper(")
        self.assert_present("function syncStepperVisual(")

    def test_stepper_disabled_state(self):
        # 钳位到 min/max 时按钮变灰
        self.assert_present(".stepper-btn.disabled")


class SelCardsBindingTests(TouchUnifyTests):
    def test_selcard_helpers_defined(self):
        self.assert_present("function selectCard(")
        self.assert_present("function syncSelCardsVisual(")

    def test_setVal_syncs_visual(self):
        # setVal 必须同时刷新绑定的 stepper / sel-cards 视觉
        self.assertRegex(SRC, r"function setVal\([^)]*\)\{[^}]*syncStepperVisual")
        self.assertRegex(SRC, r"function setVal\([^)]*\)\{[^}]*syncSelCardsVisual")


class NagModeCardsTests(TouchUnifyTests):
    def test_nag_mode_exposes_all_four_stable_modes(self):
        # 裸 row+label+select 已替换为 setting-row + 四模式 sel-cards。
        self.assert_absent('<div class="row">\n    <label>NAG 模式</label>')
        self.assert_present('data-for="nag-mode-select"')
        self.assertRegex(SRC, r'<select[^>]*id="nag-mode-select"[^>]*display:none')
        for option in [
            '<option value="0">Off</option>',
            '<option value="1">Mode A</option>',
            '<option value="2">Mode B</option>',
            '<option value="3">Mode C</option>',
        ]:
            self.assert_present(option)
        for mode in range(4):
            self.assert_present(f'onclick="selectCard(\'nag-mode-select\',{mode})"')
            self.assert_present(f'data-value="{mode}"')
        self.assert_present('class="sel-cards c4 nag-mode-cards"')
        # 旧裸 select（无 display:none）不应再存在为可见控件。
        self.assert_absent('<select id="nag-mode-select" onchange="saveDefenseConfig()">')

    def test_nag_mode_load_and_parent_disable_preserve_selection(self):
        load = re.search(r"async function loadDefenseConfig\(\)\{.*?setText\('tb-exp'", SRC, re.S)
        self.assertIsNotNone(load)
        self.assertIn("setVal('nag-mode-select'", load.group(0))
        self.assertIn("syncNagModeAvailability(!!d.enabled)", load.group(0))

        sync = re.search(r"function syncNagModeAvailability\([^)]*\)\{.*?\n\}", SRC, re.S)
        self.assertIsNotNone(sync)
        self.assertIn("classList.remove('inactive')", sync.group(0))
        self.assertIn("sel.disabled=false", sync.group(0))
        self.assertNotIn(".value=", sync.group(0))

        save = re.search(r"async function saveDefenseConfig\(\)\{.*?\n\}", SRC, re.S)
        self.assertIsNotNone(save)
        self.assertIn("nagMode: parseInt(val('nag-mode-select')||'0',10)", save.group(0))


class ApDelayCardsTests(TouchUnifyTests):
    def test_ap_delay_cards_present(self):
        self.assert_present('data-for="ap-delay-select"')
        self.assert_present("function setApDelayCards(")
        self.assert_present("function updateApDelayCards(")
    def test_ap_delay_hidden_select_kept(self):
        # 两处 select 都保留 hidden 作为 .value / class sync 载体
        self.assertRegex(SRC, r'<select[^>]*id="ap-delay-select"[^>]*display:none')
        self.assert_present('class="ap-delay-select"')


class InstantEngageToggleTests(TouchUnifyTests):
    def test_desktop_and_mobile_controls_are_present(self):
        self.assertEqual(SRC.count('class="ap-instant-edge-tgl"'), 2)
        self.assertEqual(SRC.count('Instant Engage (experimental)'), 2)
        self.assertEqual(
            SRC.count('Allow the first eligible injection immediately when AP truly becomes engaged.'),
            2,
        )

    def test_sync_preserves_checked_value_when_parent_is_disabled(self):
        sync = re.search(r"function syncInstantEngage\([^)]*\)\{.*?\n\}", SRC, re.S)
        self.assertIsNotNone(sync)
        body = sync.group(0)
        self.assertIn("document.querySelectorAll('.ap-instant-edge-tgl')", body)
        self.assertIn("el.checked=!!value", body)
        self.assertIn("el.disabled=!parentEnabled", body)
        self.assertIn("classList.toggle('inactive',!parentEnabled)", body)
        self.assertNotIn("checked=false", body)

    def test_save_uses_existing_config_and_restores_backend_on_failure(self):
        save = re.search(r"async function saveInstantEngage\([^)]*\)\{.*?\n\}", SRC, re.S)
        self.assertIsNotNone(save)
        body = save.group(0)
        self.assertIn("postForm('/config'", body)
        self.assertIn("ap_first_edge:checked?'1':'0'", body)
        self.assertIn("loadInstantEngageConfig()", body)
        self.assertIn("showToast(T('保存失败')||'Save failed',false)", body)
        self.assertNotIn("/ap_first_edge", body)


class LegacyOffsetModeCardsTests(TouchUnifyTests):
    def test_mode_cards_present(self):
        self.assert_present('data-for="legacy-offset-mode"')
        self.assert_present("onclick=\"selectCard('legacy-offset-mode','off')\"")
        self.assert_present("onclick=\"selectCard('legacy-offset-mode','custom')\"")
    def test_mode_hidden_select_kept(self):
        self.assertRegex(SRC, r'<select[^>]*id="legacy-offset-mode"[^>]*display:none')


class StepperConversionsTests(TouchUnifyTests):
    SEVEN = ["legacy-offset-manual", "legacy-smooth-rate", "legacy-offset-inp",
             "legacy-pct-low", "legacy-pct-mid", "legacy-pct-high", "legacy-pct-vhigh"]

    def test_all_seven_have_steppers(self):
        for sid in self.SEVEN:
            self.assert_present(f'data-for="{sid}"')

    def test_seven_hidden_number_inputs(self):
        for sid in self.SEVEN:
            m = re.search(rf'<input[^>]*id="{sid}"[^>]*>', SRC)
            self.assertIsNotNone(m, f"{sid} input missing")
            self.assertIn('display:none', m.group(0), f"{sid} not hidden (no display:none)")

    def test_compact_variant_for_pct(self):
        # 4 pct steppers use the compact modifier
        self.assert_present('.stepper.compact')
        for sid in ["legacy-pct-low", "legacy-pct-mid", "legacy-pct-high", "legacy-pct-vhigh"]:
            # the compact stepper wraps the pct input
            self.assertTrue(
                re.search(rf'class="stepper compact"[^>]*data-for="{sid}"', SRC) or
                re.search(rf'data-for="{sid}"', SRC),
                f"{sid} missing stepper"
            )


class DefenseMasterSwitchTests(TouchUnifyTests):
    def test_master_tgl_added(self):
        self.assert_id_present("def-master-tgl")
        self.assert_present("FSD 防护总开关")

    def test_stale_comment_removed(self):
        self.assert_absent("<!-- Master switch + 5 defense toggles -->")

    def test_wiring_uses_master_not_slew(self):
        # saveDefenseConfig 的 enabled 字段必须读取 def-master-tgl（通过 master 变量）
        m = re.search(r"async function saveDefenseConfig\(\)\{.*?\n\}", SRC, re.S)
        self.assertIsNotNone(m, "saveDefenseConfig 未找到")
        body = m.group(0)
        # master 变量从 def-master-tgl 取值
        self.assertRegex(body, r"var master=\$\('def-master-tgl'\)")
        # enabled: 读 master（不读 hw3-slew-tgl）
        self.assertRegex(body, r"enabled:master&&master\.checked")
        # 不允许再把 hw3-slew-tgl 用作 enabled 源
        self.assertNotRegex(body, r"enabled:.*hw3-slew-tgl")

    def test_load_writes_master(self):
        self.assert_present("def-master-tgl")


class DisclaimerPopupTests(TouchUnifyTests):
    def test_overlay_present(self):
        self.assert_id_present("disclaimer-overlay")
        self.assert_present("showDisclaimerIfNeeded")
    def test_confirm_button(self):
        self.assert_id_present("disclaimer-confirm")
    def test_channels(self):
        self.assert_present('https://t.me/+PKsCVABYQTdkZGQ1')
        self.assert_present("@Jordanjordan88")
        self.assert_present("ATLAS")
    def test_invoked_on_load(self):
        self.assert_present("showDisclaimerIfNeeded()")
    def test_confirm_binding_not_top_level_iife(self):
        # The confirm-button listener must NOT be bound via a top-level IIFE
        # (which runs before the overlay DOM exists). It must live inside DOMContentLoaded.
        self.assert_absent("(function(){\n  var c=$('disclaimer-confirm')")
        self.assert_absent("var c=$('disclaimer-confirm'); if(c)c.addEventListener('click',hideDisclaimer);\n})();")
        # And the binding must still exist somewhere (inside DOMContentLoaded)
        self.assert_present("addEventListener('click',hideDisclaimer)")


class I18nTests(TouchUnifyTests):
    def test_new_strings_have_en(self):
        # I18N dict uses single-quoted keys followed by a colon: 'zh':'en'
        # Keys must match the EXACT zh strings as they appear in the markup.
        for zh in [
            "FSD 防护总开关",
            "启用全部防护子项（NAG 抑制 / DND / slew / BanShield 等）。关闭即全部失效。",
            "免责声明 · Disclaimer",
            "确认 · 我已知晓",
        ]:
            self.assert_present(f"'{zh}':")


if __name__ == "__main__":
    unittest.main()
