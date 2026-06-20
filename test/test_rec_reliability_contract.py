import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
D = (ROOT / "include" / "web" / "mcp2515_dashboard.h").read_text(encoding="utf-8")
U = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class RecCapContract(unittest.TestCase):
    """Real-car: /rec_stop timed out -> 'cannot stop recording'. Root cause was
    REC_CAP=8000 (~560KB) written to an 86%-full SPIFFS (slow GC + large write).
    2000 frames (~140KB) saves fast and slows SPIFFS accumulation."""

    def test_rec_cap_reduced_to_2000(self):
        self.assertIn("#define REC_CAP 2000", D)


class RecSaveReliabilityContract(unittest.TestCase):
    """SPIFFS is log-structured; repeated overwrite of /rec.csv accumulated
    stale blocks to 86% full. Explicit remove before open frees them."""

    def test_save_removes_old_file_before_open(self):
        idx = D.find("static bool dashSaveRecordingToSpiffs")
        self.assertGreater(idx, 0)
        body = D[idx:idx + 900]
        self.assertIn('SPIFFS.remove("/rec.csv")', body)


class RecUiFeedbackContract(unittest.TestCase):
    """stopRec must show immediate progress — /rec_stop can take seconds on a
    full SPIFFS, and the old code left the UI stuck on 'Recording...' until the
    fetch resolved (or timed out)."""

    def test_stoprec_shows_saving_progress(self):
        idx = U.find("async function stopRec()")
        self.assertGreater(idx, 0)
        body = U[idx:idx + 450]
        self.assertTrue("保存中" in body or "Saving" in body)


if __name__ == "__main__":
    unittest.main()
