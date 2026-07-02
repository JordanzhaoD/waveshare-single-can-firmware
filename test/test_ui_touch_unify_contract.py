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


if __name__ == "__main__":
    unittest.main()
