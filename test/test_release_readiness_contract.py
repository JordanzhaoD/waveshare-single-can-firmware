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

    def test_release_profile_enables_ap_injection_gate(self) -> None:
        profile = re.search(
            r"profile: (.*?)(?=\n\s+project_dir:)",
            self.release_workflow,
            re.DOTALL,
        )
        self.assertIsNotNone(profile)
        self.assertIn("--enable INJECTION_AFTER_AP", profile.group(1))

    def test_release_checks_generated_and_bundled_artifacts(self) -> None:
        build = self.release_workflow.index("pio run -e ${{ matrix.env }}")
        generated_check = self.release_workflow.index(
            "python scripts/check_flash_artifacts.py", build
        )
        bundle = self.release_workflow.index("scripts/build_release_assets.sh")
        release_check = self.release_workflow.index(
            "--release-dir release-assets", bundle
        )
        self.assertGreater(generated_check, build)
        self.assertGreater(release_check, bundle)

    def test_release_remains_draft_until_manual_approval(self) -> None:
        self.assertRegex(self.release_workflow, r"(?m)^\s+draft: true\s*$")
        self.assertNotIn("Publish draft release", self.release_workflow)
        self.assertNotIn("--draft=false", self.release_workflow)

    def test_flash_script_verifies_bundle_checksums(self) -> None:
        self.assertIn("SHA256SUMS", self.flash_template)
        self.assertRegex(self.flash_template, r"sha256sum|shasum")

    def test_flash_script_explains_nvs_behavior(self) -> None:
        self.assertRegex(self.flash_template, r"(?i)merged.*NVS")
        self.assertRegex(self.flash_template, r"(?i)--split.*NVS")


if __name__ == "__main__":
    unittest.main()
