import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "_config.yml"
PAGES_WORKFLOW = ROOT / ".github" / "workflows" / "pages.yml"


class PagesConfigTests(unittest.TestCase):
    def test_jekyll_excludes_internal_superpowers_docs(self) -> None:
        config = CONFIG.read_text(encoding="utf-8")
        self.assertRegex(
            config,
            r"(?m)^exclude:\s*(?:\n\s*-\s*[^\n]+)*\n\s*-\s*docs/superpowers\s*$",
            "Jekyll must exclude internal docs/superpowers plans/specs; "
            "they contain Liquid-looking code snippets such as {{...}}.",
        )

    def test_pages_workflow_runs_when_jekyll_config_changes(self) -> None:
        workflow = PAGES_WORKFLOW.read_text(encoding="utf-8")
        self.assertRegex(
            workflow,
            r"(?m)^\s*-\s*_config\.yml\s*$",
            "Pages workflow path filters must include _config.yml so config fixes are verified in CI.",
        )


if __name__ == "__main__":
    unittest.main()
