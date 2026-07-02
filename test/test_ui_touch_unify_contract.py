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


if __name__ == "__main__":
    unittest.main()
