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


def frame_id_blocks(source: str, frame_id: int) -> list[str]:
    blocks = []
    token = f"frame.id == {frame_id}"
    search_from = 0
    while True:
        condition = source.find(token, search_from)
        if condition == -1:
            return blocks
        block_start = source.find("{", condition)
        if block_start == -1:
            return blocks
        depth = 0
        for idx in range(block_start, len(source)):
            if source[idx] == "{":
                depth += 1
            elif source[idx] == "}":
                depth -= 1
                if depth == 0:
                    blocks.append(source[condition : idx + 1])
                    search_from = idx + 1
                    break
        else:
            return blocks


class NoEpasNagContract(unittest.TestCase):
    def setUp(self) -> None:
        self.ini = (ROOT / "platformio.ini").read_text()
        self.helpers = (ROOT / "include" / "can_helpers.h").read_text()
        self.handlers = (ROOT / "include" / "handlers.h").read_text()
        self.reactive = (ROOT / "include" / "dash_reactive_nag.h").read_text()
        self.reactive_hold = (ROOT / "include" / "dash_reactive_hold_nag.h").read_text()
        self.echo_builder = (ROOT / "include" / "dash_legacy_370_echo.h").read_text()
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
        """DashReactiveNagBurst v4 (TSL6P Burst NAG) must be opt-in, bounded, and 0x399 read-only."""
        # bionicSteering member defaults false (on CarManagerBase)
        self.assertRegex(
            self.handlers,
            r"Shared<bool>\s+bionicSteering\s*\{[^}]*\bfalse\b",
            "bionicSteering must default to false",
        )

        # LegacyHandler 0x370 replay echo gated on active + nag.shouldEcho.
        for token in ['gateReason = "toggle"', 'gateReason = "apInactive"', 'gateReason = "checkAD"']:
            self.assertIn(token, self.handlers, "reactive echo must expose the honest gate-loss reason")
        self.assertIn(
            "bool replayPending = nag.shouldEcho(nowMs)",
            self.handlers,
            "reactive echo must first detect pending replay",
        )
        self.assertIn(
            "bool useReplay = replayPending && active",
            self.handlers,
            "reactive echo must be gated on pending replay + combined opt-in/AP/checkAD active state",
        )
        self.assertIn("bool checkAdAllowed = !(checkAD && !checkAD())", self.handlers, "reactive echo must be gated on checkAD")
        self.assertIn("nag.advance(nowMs, active, gateReason)", self.handlers, "0x370 gate loss must cancel via explicit reason")
        self.assertIn("uint32_t nowMs = dashDiagNowMs();", self.handlers, "0x399 must stage one diagnostic timestamp")
        self.assertIn("nag.onNagSample(hos, nowMs, active, apState, gateReason)", self.handlers, "legacy 0x399 gate loss must cancel via explicit reason")

        # LegacyHandler 0x399 NAG detection reads byte5 bits[5:2] and does not transmit/mutate 0x399.
        block_start = self.legacy.index("if (frame.id == 921)")
        block_end = self.legacy.index("// 0x3EE", block_start)
        block = self.legacy[block_start:block_end]
        self.assertIn("(frame.data[5] >> 2) & 0x0F", block, "0x399 handsOn decode must read byte5 bits[5:2]")
        self.assertIn("nag.onNagSample", block, "0x399 should only feed NAG samples")
        self.assertNotIn("driver.send", block, "0x399 must not be transmitted/spoofed")
        self.assertIsNone(
            re.search(r"frame\.data\[[^\]]+\]\s*[-+*/%&|^]?=(?!=)", block),
            "0x399 block must not mutate any frame.data byte",
        )

        # Shared 0x370 builder owns hands-on, counter, and checksum framing.
        echo_start = self.legacy.index("if (frame.id == 880")
        echo_end = self.legacy.index("// STW_ACTN_RQ", echo_start)
        echo_block = self.legacy[echo_start:echo_end]
        self.assertIn("dashBuildLegacy370Echo(frame, d2lo, d3, true)", echo_block)
        self.assertIn("(source.data[4] & 0x3F) | 0x40", self.echo_builder)

        self.assertIn("DashReactiveNagBurst", self.reactive)
        for token in ["kBurstOnMs{1000}", "kBurstOffMs{1500}", "kMaxSignedOutRaw{180}"]:
            self.assertIn(token, self.reactive, f"v4 hard bound missing: {token}")

        # Mode 3 is explicit, opt-in, bounded, and only sampled from 0x399.
        self.assertIn("DashReactiveHoldNag reactiveHoldNag", self.legacy)
        self.assertIn("DashNagMode::ReactiveHold", self.legacy)
        self.assertIn('std::strcmp(mode, "reactive_hold")', self.legacy)
        self.assertIn("reactiveHoldNag.onNagSample(hos, nowMs, active)", block)
        for token in [
            "kReactiveAmp = 70",
            "kReactiveJitter = 15",
            "kProactiveAmp = 35",
            "kProactiveJitter = 12",
            "kReactiveCooldownMs = 800",
            "kProactiveIntervalMinMs = 2000",
            "kProactiveIntervalMaxMs = 5000",
            "std::min(95, value)",
            "std::min(0x0FFF, torque + kHumanWeight + clampHold(hold))",
        ]:
            self.assertIn(token, self.reactive_hold, f"Reactive Hold bound missing: {token}")
        for token in [
            "reactiveHoldNag.shouldEcho(nowMs)",
            "reactiveHoldNag.computeHold(nowMs)",
            "reactiveHoldNag.applyToFrame(d2lo, d3, hold)",
            "reactiveHoldNag.notifyEchoSent()",
        ]:
            self.assertIn(token, echo_block)

    def test_reactive_hold_send_is_abort_guarded_and_counts_only_success(self) -> None:
        echo_start = self.legacy.index("if (frame.id == 880")
        echo_end = self.legacy.index("// STW_ACTN_RQ", echo_start)
        echo_block = self.legacy[echo_start:echo_end]
        reactive_start = echo_block.index("if (reactiveHoldSelected())")
        reactive_end = echo_block.index("if (!humanReplaySelected())", reactive_start)
        reactive = echo_block[reactive_start:reactive_end]
        guard_idx = reactive.index("abortGuard.allowsInjection()")
        send_idx = reactive.index("if (driver.send(echo))")
        notify_idx = reactive.index("reactiveHoldNag.notifyEchoSent()")
        self.assertLess(guard_idx, send_idx)
        self.assertGreater(notify_idx, send_idx)
        self.assertIn("DashAbortGuardBlockPath::Nag", reactive[:send_idx])

    def test_legacy_speed_safety_v2_does_not_add_forbidden_writes(self) -> None:
        """Smart Legacy speed may only write 0x2F8; Abort Guard may not introduce new write paths."""
        forbidden_ids = [921, 0x399, 817, 0x331, 1016, 0x3F8]
        for frame_id in forbidden_ids:
            for branch in frame_id_blocks(self.legacy, frame_id):
                with self.subTest(frame_id=frame_id):
                    self.assertNotRegex(branch, r'\bdriver\.send\s*\(')

        legacy_760 = re.search(r'if \(frame\.id == 760\).*?if \(frame\.id == 1080\)', self.legacy, re.S)
        self.assertIsNotNone(legacy_760)
        block = legacy_760.group(0)
        self.assertRegex(block, r'frame\.data\[5\]\s*(?:=|[|&^+\-]=)')
        for var_name in ['frame', 'out', 'echo']:
            for idx in [0, 1, 2, 3, 4, 6, 7]:
                with self.subTest(var_name=var_name, idx=idx):
                    self.assertIsNone(re.search(rf'\b{var_name}\.data\[{idx}\]\s*(?:=|[|&^+\-]=)', block))


class Tsl6pBurstNagV4Contract(unittest.TestCase):
    def setUp(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.reactive = (root / "include" / "dash_reactive_nag.h").read_text()
        self.handlers = (root / "include" / "handlers.h").read_text()
        self.legacy = legacy_handler_block(self.handlers)

    def test_v4_burst_has_hard_timing_and_torque_bounds(self) -> None:
        for token in ["kBurstOnMs{1000}", "kBurstOffMs{1500}", "kAbortCooldownMs{3000}", "kMaxSignedOutRaw{180}"]:
            self.assertIn(token, self.reactive)

    def test_v4_tsl6p_sequence_is_bounded_and_bidirectional(self) -> None:
        match = re.search(r"TSL6P_TORQUE_RAW\s*\[[^\]]+\]\s*=\s*\{([^}]*)\}", self.reactive, re.S)
        self.assertIsNotNone(match, "TSL6P_TORQUE_RAW initializer must exist")
        torque_sequence = [int(value) for value in re.findall(r"-?\d+", match.group(1))]
        self.assertEqual(torque_sequence, [180, 150, -150, -180])
        self.assertNotIn("kMaxSignedOutRaw{220}", self.reactive, "v4 NAG torque path must not keep the v3 2.20Nm cap")
        self.assertNotIn("kMaxSignedOutRaw{250}", self.reactive)
        self.assertNotIn("kMaxDeltaRaw", self.reactive)

    def test_v4_does_not_transmit_or_mutate_0x399(self) -> None:
        block_start = self.legacy.index("if (frame.id == 921)")
        block_end = self.legacy.index("// 0x3EE", block_start)
        block = self.legacy[block_start:block_end]
        self.assertIn("nag.onNagSample", block)
        self.assertIn("lateNag.onDasStatus", block)
        self.assertIn("apState", block)
        self.assertRegex(
            block,
            r"nag\.onNagSample\(\s*hos\s*,\s*nowMs\s*,\s*active\s*,\s*apState\s*,\s*gateReason\s*\)",
        )
        self.assertNotIn("driver.send", block)
        self.assertIsNone(re.search(r"frame\.data\[[^\]]+\]\s*[-+*/%&|^]?=(?!=)", block))
        self.assertNotIn("echo.id = 921", self.handlers)

    def test_v4_legacy_echo_remains_opt_in_and_checkad_gated(self) -> None:
        block_start = self.legacy.index("if (frame.id == 880")
        block_end = self.legacy.index("// STW_ACTN_RQ", block_start)
        block = self.legacy[block_start:block_end]
        for token in ["bionicSteering", "APActive", "checkAD", "gateReason", "humanReplaySelected", "nag.shouldEcho", "nag.peekReplayDelta", "nag.commitReplayDelta", "nag.failReplayTx", "nag.advance"]:
            self.assertIn(token, block)
        self.assertIn("nag.applyToFrame", block)
        self.assertIn("dashBuildLegacy370Echo(frame, d2lo, d3, true)", block)

    def test_v4_legacy_echo_is_abort_guard_gated_before_send(self) -> None:
        block_start = self.legacy.index("if (frame.id == 880")
        block_end = self.legacy.index("// STW_ACTN_RQ", block_start)
        block = self.legacy[block_start:block_end]
        send_idx = block.index("driver.send(echo)")
        guard_idx = block.index("abortGuard.allowsInjection()")
        self.assertLess(guard_idx, send_idx)
        self.assertIn("DashAbortGuardBlockPath::Nag", block[:send_idx])

    def test_v4_abort_guard_blocks_states_8_and_9(self) -> None:
        self.assertIn("apState == 8 || apState == 9", self.reactive)
        self.assertIn("enterAbortCooldown", self.reactive)
        self.assertIn('"abort"', self.reactive)


class EpasLateEchoContract(unittest.TestCase):
    def setUp(self) -> None:
        root = Path(__file__).resolve().parents[1]
        self.handlers = (root / "include" / "handlers.h").read_text()
        self.late = (root / "include" / "dash_epas_late_echo.h").read_text()
        self.legacy = legacy_handler_block(self.handlers)

    def test_late_echo_preserves_370_byte4(self) -> None:
        self.assertIn("out.data[4] = source.data[4]", self.late)
        self.assertNotIn("| 0x40", self.late)
        self.assertIn("preserveHandsOnLevel", self.late)

    def test_late_echo_hard_caps_torque(self) -> None:
        self.assertRegex(self.late, r"kMaxTorqueRaw\s*=\s*180")
        self.assertIn("targetTorqueRaw > kMaxTorqueRaw", self.late)
        self.assertIn("targetTorqueRaw < -kMaxTorqueRaw", self.late)
        self.assertNotIn("kMaxTorqueRaw{220}", self.late)
        self.assertNotIn("kMaxTorqueRaw{250}", self.late)

    def test_late_echo_uses_tick_not_immediate_send(self) -> None:
        epas_branch_start = self.legacy.index("if (frame.id == 880)")
        late_branch_start = self.legacy.index("if (lateEchoSelected())", epas_branch_start)
        late_branch_end = self.legacy.index("if (frame.dlc < 8)", late_branch_start)
        late_branch = self.legacy[late_branch_start:late_branch_end]
        self.assertIn("lateNag.onEpasFrame", late_branch)
        self.assertNotIn("driver.send", late_branch)
        self.assertIn("void tick(uint32_t nowMs, CanDriver &driver) override", self.legacy)
        self.assertIn("lateNag.buildDueFrame", self.legacy)

    def test_late_echo_tick_is_abort_guard_gated_before_send(self) -> None:
        tick_start = self.legacy.index("void tick(uint32_t nowMs, CanDriver &driver) override")
        tick_end = self.legacy.index("void tick(CanDriver &driver)", tick_start)
        tick = self.legacy[tick_start:tick_end]
        send_idx = tick.index("driver.send(echo)")
        guard_idx = tick.index("abortGuard.allowsInjection()")
        self.assertLess(guard_idx, send_idx)
        self.assertIn("DashAbortGuardBlockPath::Nag", tick[:send_idx])
        self.assertIn("lateNag.notifyTxResult(token, false, nowMs)", tick[:send_idx])

    def test_late_echo_does_not_write_399_129_or_39b(self) -> None:
        mutation_pattern = r"frame\.data\[[^\]]+\]\s*[-+*/%&|^]?=(?!=)"

        for forbidden in ["echo.id = 921", "out.id = 921", "echo.id = 297", "out.id = 297", "echo.id = 923", "out.id = 923"]:
            self.assertNotIn(forbidden, self.legacy + self.late)

        block_start = self.legacy.index("if (frame.id == 921)")
        block_end = self.legacy.index("// 0x3EE", block_start)
        block = self.legacy[block_start:block_end]
        self.assertNotIn("driver.send", block)
        self.assertIsNone(re.search(mutation_pattern, block))

        for frame_id in [297, 923]:
            for branch in frame_id_blocks(self.legacy, frame_id):
                self.assertNotIn("driver.send", branch)
                self.assertIsNone(re.search(mutation_pattern, branch))
