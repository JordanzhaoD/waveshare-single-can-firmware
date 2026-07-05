"""契约测试：手机底部标签栏 hoist 修复。

根因：#mob-tabs 等 position:fixed 元素原本嵌套在 .content (overflow-y:auto) 滚动
容器内。移动端浏览器（iOS Safari 等）在长页面 momentum scroll 时无法重绘 fixed
元素，导致切换到 硬件/速度/网络/防护 后底部标签栏变空白。

修复：在 DOMContentLoaded 把这些 fixed chrome 提升为 <body> 直接子元素，使
position:fixed 稳定锚定到 viewport。
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class MobileTabHoistTests(unittest.TestCase):
    def test_hoist_function_defined(self):
        """hoistMobileChrome() 必须定义，且覆盖全部 5 个 fixed chrome 元素。"""
        self.assertIn("function hoistMobileChrome(", SRC)
        for element_id in [
            "mob-tabs",
            "mob-more-single",
            "mob-tabs-dual",
            "mob-more",
            "disclaimer-overlay",
        ]:
            self.assertIn("'%s'" % element_id, SRC, f"hoistMobileChrome 未覆盖 #{element_id}")

    def test_hoist_called_in_DOMContentLoaded(self):
        """必须在 DOMContentLoaded 监听器内最先调用 hoistMobileChrome()。"""
        # DOMContentLoaded 回调内首行即 hoist（在任何 querySelectorAll 绑定之前）
        self.assertRegex(
            SRC,
            r"addEventListener\('DOMContentLoaded',function\(\)\{\s*hoistMobileChrome\(\);",
            "hoistMobileChrome() 必须在 DOMContentLoaded 最开头调用",
        )

    def test_mobile_tab_bar_uses_fixed_position(self):
        """标签栏仍为 position:fixed（hoist 不改变定位策略，只改变 DOM 父级）。"""
        self.assertRegex(SRC, r"\.mob-tabs\s*\{[^}]*position:\s*fixed")
        self.assertRegex(SRC, r"\.mob-tabs\s*\{[^}]*bottom:\s*0")

    def test_mob_tabs_has_dual_hide_attr(self):
        """#mob-tabs 必须自带 data-dual-hide，hoist 脱离 wrapper 后仍由
        applyProductMode 直接控制显隐（单 CAN 显示、双 CAN 隐藏）。"""
        self.assertRegex(SRC, r'<div class="mob-tabs" id="mob-tabs" data-dual-hide="1">')

    def test_inline_onclick_handlers_intact(self):
        """内联 onclick 处理器保留（hoist 后仍生效）。"""
        for page in ["driving", "hardware", "speed", "network", "defense"]:
            self.assertIn("onclick=\"showMobilePage('%s')\"" % page, SRC,
                          f"缺失 {page} 标签的 onclick")


if __name__ == "__main__":
    unittest.main()
