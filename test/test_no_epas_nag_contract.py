#!/usr/bin/env python3
"""Guard rail: reactive NAG-suppression is opt-in + bounded; NagHandler tamper stays opt-in.

History: the old DashEpasNagEngine (spoofed 0x370 EPAS own-status frames) caused a
real-vehicle body-control fault (2026-06-19) and was physically removed. Jordan
authorized on 2026-06-25: (1) lift the 0x370 symbol-ban, (2) ignore the Mode-C
incident — to enable porting the public-source reactive torque-burst technique
(DashReactiveNagBurst). This file now guards the FORWARD-looking safety contract
for that reactive path (opt-in, bounded, amplitude-capped) plus the existing
NagHandler torque-tamper opt-in guard.

Run: python3 -m pytest test/test_no_epas_nag_contract.py -q
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def legacy_handler_block(handlers: str) -> str:
    block_start = handlers.index("struct LegacyHandler")
    block_end = handlers.index("struct HW3Handler", block_start)
    return handlers[block_start:block_end]


class NoEpasNagContract(unittest.TestCase):
    def setUp(self) -> None:
        self.ini = (ROOT / "platformio.ini").read_text()
        self.helpers = (ROOT / "include" / "can_helpers.h").read_text()
        self.handlers = (ROOT / "include" / "handlers.h").read_text()
        self.reactive = (ROOT / "include" / "dash_reactive_nag.h").read_text()
        self.legacy = legacy_handler_block(self.handlers)

    def test_no_native_epas_nag_env(self) -> None:
        self.assertNotIn("[env:native_epas_nag]", self.ini, "native_epas_nag env must stay removed")

    def test_nag_torque_tamper_global_defaults_false(self) -> None:
        """nagTorqueTamperRuntime must be declared with a false default (passthrough)."""
        self.assertIn("nagTorqueTamperRuntime", self.helpers, "nagTorqueTamperRuntime global must exist")
        self.assertRegex(
            self.helpers,
            r"nagTorqueTamperRuntime\s*\{[^}]*\bfalse\b",
            "nagTorqueTamperRuntime must be declared with a false default (passthrough is the default mode)",
        )

    def test_nag_torque_tamper_is_gated_behind_opt_in(self) -> None:
        """The 0x370 torque-tamper (0xB6) must live inside the opt-in branch."""
        self.assertIn(
            "if (nagTorqueTamperRuntime)",
            self.handlers,
            "tamper path must be gated on nagTorqueTamperRuntime",
        )
        self.assertIn("0xB6", self.handlers, "torque-tamper constant 0xB6 must be present in the opt-in branch")
        # The branch must carry the 2026-06-19 incident warning.
        self.assertIn("2026-06-19", self.handlers, "torque-tamper branch must reference incident warning date")
        self.assertIn("EPAS", self.handlers, "torque-tamper branch must reference the EPAS incident warning")

    def test_nag_torque_tamper_constant_not_in_default_path(self) -> None:
        """The default (else) path must NOT write 0xB6 — it passes torque through."""
        idx_if = self.handlers.find("if (nagTorqueTamperRuntime)")
        self.assertNotEqual(idx_if, -1, "opt-in branch missing")
        idx_else = self.handlers.find("else", idx_if)
        self.assertNotEqual(idx_else, -1, "else (passthrough) branch missing")
        tamper_block = self.handlers[idx_if:idx_else]
        passthrough_block = self.handlers[idx_else:idx_else + 400]
        self.assertIn("0xB6", tamper_block, "0xB6 must be inside the opt-in branch")
        self.assertNotIn(
            "0xB6",
            passthrough_block,
            "0xB6 torque-tamper must NOT appear in the default (passthrough) path",
        )

    def test_reactive_nag_is_optin_and_bounded(self) -> None:
        """DashReactiveNagBurst v3 (Human Torque Replay) must be opt-in, bounded, and 0x399 read-only."""
        # bionicSteering member defaults false (on CarManagerBase)
        self.assertRegex(
            self.handlers,
            r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b",
            "bionicSteering must default to false",
        )

        # LegacyHandler 0x370 replay echo gated on active + nag.shouldEcho.
        self.assertIn(
            "bool active = (bool)bionicSteering && APActive",
            self.handlers,
            "reactive echo must be gated on bionicSteering + APActive",
        )
        self.assertIn(
            "bool replayPending = nag.shouldEcho(nowMs)",
            self.handlers,
            "reactive echo must first detect pending replay",
        )
        self.assertIn(
            "bool useReplay = replayPending && active && checkAdAllowed",
            self.handlers,
            "reactive echo must be gated on pending replay + active + checkAD",
        )
        self.assertIn("bool checkAdAllowed = !(checkAD && !checkAD())", self.handlers, "reactive echo must be gated on checkAD")
        self.assertIn("nag.cancel(\"toggle\")", self.handlers, "active/toggle gate loss must cancel replay")
        self.assertIn("nag.cancel(\"checkAD\")", self.handlers, "checkAD gate loss must cancel replay")

        # LegacyHandler 0x399 NAG detection reads byte5 bits[5:2] and does not transmit/mutate 0x399.
        block_start = self.legacy.index("if (frame.id == 921)")
        block_end = self.legacy.index("// 0x3EE", block_start)
        block = self.legacy[block_start:block_end]
        self.assertIn("(frame.data[5] >> 2) & 0x0F", block, "0x399 handsOn decode must read byte5 bits[5:2]")
        self.assertIn("nag.onNagSample", block, "0x399 should only feed NAG samples")
        self.assertNotIn("driver.send", block, "0x399 must not be transmitted/spoofed")
        self.assertIsNone(
            re.search(r"frame\.data\[[^\]]+\]\s*[-+*/%&|^]?=", block),
            "0x399 block must not mutate any frame.data byte",
        )

        # data[4] handsOnLevel=1 is forged only inside the LegacyHandler 0x370 echo block.
        echo_start = self.legacy.index("if (frame.id == 880")
        echo_end = self.legacy.index("// STW_ACTN_RQ", echo_start)
        echo_block = self.legacy[echo_start:echo_end]
        self.assertIn(
            "(frame.data[4] & 0x3F) | 0x40",
            echo_block,
            "data[4] handsOnLevel=1 must be forged on 0x370 echo",
        )
        self.assertIn("DashReactiveNagBurst", self.reactive)
        for token in ["kMaxAttempts{3}", "kCooldownMs{3000}", "kMaxDeltaRaw{180}", "kMaxSignedOutRaw{220}"]:
            self.assertIn(token, self.reactive, f"v3 hard bound missing: {token}")


class HumanTorqueReplayV3Contract(unittest.TestCase):
    def setUp(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.reactive = (root / "include" / "dash_reactive_nag.h").read_text()
        self.handlers = (root / "include" / "handlers.h").read_text()
        self.legacy = legacy_handler_block(self.handlers)

    def test_v3_replay_has_hard_attempt_and_torque_bounds(self) -> None:
        self.assertIn("kMaxAttempts{3}", self.reactive)
        self.assertIn("kCooldownMs{3000}", self.reactive)
        self.assertIn("kMaxDeltaRaw{180}", self.reactive)
        self.assertIn("kMaxSignedOutRaw{220}", self.reactive)

    def test_v3_replay_profiles_are_bounded_and_bidirectional(self) -> None:
        for token in ["POS_MED", "NEG_MED", "POS_STRONG", "NEG_STRONG"]:
            self.assertIn(token, self.reactive)
        self.assertIn("175", self.reactive)
        self.assertIn("-175", self.reactive)
        self.assertNotIn("250", self.reactive)

    def test_v3_does_not_transmit_or_mutate_0x399(self) -> None:
        block_start = self.legacy.index("if (frame.id == 921)")
        block_end = self.legacy.index("// 0x3EE", block_start)
        block = self.legacy[block_start:block_end]
        self.assertIn("nag.onNagSample", block)
        self.assertNotIn("driver.send", block)
        self.assertIsNone(re.search(r"frame\.data\[[^\]]+\]\s*[-+*/%&|^]?=", block))
        self.assertNotIn("echo.id = 921", self.handlers)

    def test_v3_legacy_echo_remains_opt_in_and_checkad_gated(self) -> None:
        block_start = self.legacy.index("if (frame.id == 880")
        block_end = self.legacy.index("// STW_ACTN_RQ", block_start)
        block = self.legacy[block_start:block_end]
        self.assertIn("bionicSteering", block)
        self.assertIn("APActive", block)
        self.assertIn("checkAD", block)
        self.assertIn("nag.shouldEcho", block)
        self.assertIn("nag.peekReplayDelta", block)
        self.assertIn("nag.commitReplayDelta", block)
        self.assertIn("nag.cancel", block)
        self.assertIn("(frame.data[4] & 0x3F) | 0x40", block)
