import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))
import decode_epas_capture as dec


class TestDecodeEpasCapture(unittest.TestCase):
    def test_parses_one_hex_line(self):
        line = ("\U0001f4aa [CAN A - 0x370] 方向盘实际受力: +0.13 Nm "
                "\t(HEX: 52 08 08 0F 20 2C 25 55 )")
        frames = dec.parse_frames(line)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0][0], [0x52, 0x08, 0x08, 0x0F, 0x20, 0x2C, 0x25, 0x55])

    def test_expect_echo_true_when_handson_bits_zero(self):
        # byte4 = 0x20 -> bits 7:6 = 00 -> expectEcho True
        line = "(HEX: 52 08 08 0F 20 2C 25 55 )"
        self.assertTrue(dec.parse_frames(line)[0][1])

    def test_expect_echo_false_when_handson_bits_nonzero(self):
        # byte4 = 0xC0 -> bits 7:6 = 11 -> expectEcho False
        line = "(HEX: 52 08 08 0F C0 2C 25 99 )"
        self.assertFalse(dec.parse_frames(line)[0][1])

    def test_ignores_non_hex_lines(self):
        self.assertEqual(dec.parse_frames("noise\n<<< dismissed\n"), [])

    def test_tag_increments_on_warning_marker(self):
        text = ("\U0001f6a8❗ [monitor] 检测到握盘警告。\n"
                "(HEX: 52 08 08 0F 20 2C 25 55 )\n")
        frames = dec.parse_frames(text)
        self.assertEqual(frames[0][2], "e1")

    def test_format_header_has_array_and_a_byte(self):
        h = dec.format_header([([0x52, 0x08, 0x08, 0x0F, 0x20, 0x2C, 0x25, 0x55], True, "e1")])
        self.assertIn("kRealEpasSamples[]", h)
        self.assertIn("0x52", h)
        self.assertIn("true", h)


if __name__ == "__main__":
    unittest.main()
