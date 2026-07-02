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
    def test_nag_mode_converted_to_cards(self):
        # 裸 row+label+select 已替换为 setting-row + sel-cards
        self.assert_absent('<div class="row">\n    <label>NAG 模式</label>')
        self.assert_present('data-for="nag-mode-select"')
        # 隐藏 select 保留作 .value 载体
        self.assertRegex(SRC, r'<select[^>]*id="nag-mode-select"[^>]*display:none')
        # 两张卡片
        self.assert_present('onclick="selectCard(\'nag-mode-select\',0)"')
        self.assert_present('onclick="selectCard(\'nag-mode-select\',2)"')
        # 旧裸 select（无 display:none）不应再存在为可见控件
        self.assert_absent('<select id="nag-mode-select" onchange="saveDefenseConfig()">')


class ApDelayCardsTests(TouchUnifyTests):
    def test_ap_delay_cards_present(self):
        self.assert_present('data-for="ap-delay-select"')
        self.assert_present("function setApDelayCards(")
        self.assert_present("function updateApDelayCards(")
    def test_ap_delay_hidden_select_kept(self):
        # 两处 select 都保留 hidden 作为 .value / class sync 载体
        self.assertRegex(SRC, r'<select[^>]*id="ap-delay-select"[^>]*display:none')
        self.assert_present('class="ap-delay-select"')


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


if __name__ == "__main__":
    unittest.main()
