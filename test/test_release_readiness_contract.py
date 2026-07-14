import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TESTS_WORKFLOW = ROOT / ".github" / "workflows" / "tests.yml"
RELEASE_WORKFLOW = ROOT / ".github" / "workflows" / "release.yml"
FLASH_TEMPLATE = ROOT / "scripts" / "flash.sh.template"


class ReleaseReadinessContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tests_workflow = TESTS_WORKFLOW.read_text(encoding="utf-8")
        cls.release_workflow = RELEASE_WORKFLOW.read_text(encoding="utf-8")
        cls.flash_template = FLASH_TEMPLATE.read_text(encoding="utf-8")

    def test_tests_workflow_runs_complete_native_matrix(self) -> None:
        for environment in (
            "native_single_can_dashboard",
            "native_legacy_speed",
            "native_abort_guard",
        ):
            self.assertRegex(
                self.tests_workflow,
                rf"(?m)^\s+- {re.escape(environment)}\s*$",
            )

    def test_tests_workflow_checks_generated_flash_artifacts(self) -> None:
        build = self.tests_workflow.index("pio run -e ${{ matrix.env }}")
        checker = self.tests_workflow.index(
            "python scripts/check_flash_artifacts.py", build
        )
        self.assertGreater(checker, build)


if __name__ == "__main__":
    unittest.main()
