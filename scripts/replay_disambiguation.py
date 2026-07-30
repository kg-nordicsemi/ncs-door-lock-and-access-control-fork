#!/usr/bin/env python3
"""Replay captured disambiguation metrics through the current scoring model."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


LINE_RE = re.compile(
    r"\[(?P<h>\d+):(?P<m>\d+):(?P<s>\d+)\.(?P<ms>\d+)[^\]]*\].*?"
    r"Disambiguator: Process: sess\d+ .*?"
    r"pratio=(?P<pratio>\d+).*?uwb=(?P<uwb>-?\d+)dBm"
)

P_RATIO_LOW = 0.15
P_RATIO_FULL = 0.50
EWMA_ALPHA = 0.15
FRONT_THRESHOLD = 0.65
BACK_THRESHOLD = 0.35
RSL_ENABLE_DB = 71.0
RSL_BLOCK_DB = 73.0
RSL_DROP_DB = 5.0
RSL_REFERENCE_ALPHA = 0.03


@dataclass(frozen=True)
class Sample:
    time_s: float
    p_ratio: float
    rsl_db: float


@dataclass(frozen=True)
class Segment:
    start_s: float
    end_s: float | None
    expected_front: bool
    name: str
    max_wrong_pct: float | None = None


@dataclass
class State:
    score: float = 0.0
    final_front: bool = False
    has_been_front: bool = False
    cold_rsl_blocked: bool = True
    reference_rsl: float | None = None


@dataclass(frozen=True)
class Decision:
    time_s: float
    front: bool
    score: float
    evidence: float
    rsl_vetoed: bool


def parse_log(path: Path) -> list[Sample]:
    samples: list[Sample] = []
    for line in path.read_text(errors="replace").splitlines():
        match = LINE_RE.search(line)
        if not match:
            continue
        time_s = (
            int(match["h"]) * 3600
            + int(match["m"]) * 60
            + int(match["s"])
            + int(match["ms"]) / (10 ** len(match["ms"]))
        )
        samples.append(
            Sample(
                time_s=time_s,
                p_ratio=int(match["pratio"]) / 1_000_000.0,
                rsl_db=abs(float(match["uwb"])),
            )
        )
    return samples


def process_sample(state: State, sample: Sample) -> Decision:
    valid = sample.p_ratio > 0.0
    evidence = (
        min(max((sample.p_ratio - P_RATIO_LOW) / (P_RATIO_FULL - P_RATIO_LOW), 0.0), 1.0)
        if valid
        else 0.0
    )
    if valid:
        state.score += EWMA_ALPHA * (evidence - state.score)
        state.score = min(max(state.score, 0.0), 1.0)

    radar_front = (
        state.score >= BACK_THRESHOLD if state.final_front else state.score >= FRONT_THRESHOLD
    )

    if not state.has_been_front:
        if sample.rsl_db <= RSL_ENABLE_DB:
            state.cold_rsl_blocked = False
        elif sample.rsl_db >= RSL_BLOCK_DB:
            state.cold_rsl_blocked = True
        rsl_vetoed = state.cold_rsl_blocked
    else:
        rsl_vetoed = (
            state.reference_rsl is None
            or sample.rsl_db - state.reference_rsl >= RSL_DROP_DB
        )

    state.final_front = radar_front and not rsl_vetoed
    if state.final_front:
        state.has_been_front = True
        if state.reference_rsl is None:
            state.reference_rsl = sample.rsl_db
        elif not rsl_vetoed:
            state.reference_rsl += RSL_REFERENCE_ALPHA * (
                sample.rsl_db - state.reference_rsl
            )

    return Decision(sample.time_s, state.final_front, state.score, evidence, rsl_vetoed)


def evaluate(path: Path, segments: list[Segment]) -> bool:
    samples = parse_log(path)
    if not samples:
        print(f"{path}: no parseable samples")
        return False

    decisions: list[Decision] = []
    state = State()
    for sample in samples:
        decisions.append(process_sample(state, sample))

    print(f"\n{path}")
    passed = True
    for segment in segments:
        selected = [
            decision
            for decision in decisions
            if decision.time_s >= segment.start_s
            and (segment.end_s is None or decision.time_s < segment.end_s)
        ]
        if not selected:
            print(f"  {segment.name}: no samples")
            passed = False
            continue

        wrong = sum(decision.front != segment.expected_front for decision in selected)
        wrong_pct = 100.0 * wrong / len(selected)
        expected = "FRONT" if segment.expected_front else "BACK"
        first_expected = next(
            (decision for decision in selected if decision.front == segment.expected_front),
            None,
        )
        segment_start = max(segment.start_s, selected[0].time_s)
        latency = (
            f"{first_expected.time_s - segment_start:.3f}s"
            if first_expected is not None
            else "never"
        )
        limit = (
            f", limit={segment.max_wrong_pct:.1f}%"
            if segment.max_wrong_pct is not None
            else ", informational"
        )
        print(
            f"  {segment.name}: expected={expected}, samples={len(selected)}, "
            f"wrong={wrong} ({wrong_pct:.1f}%), first={latency}{limit}"
        )
        if segment.max_wrong_pct is not None:
            passed &= wrong_pct <= segment.max_wrong_pct
    return passed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--logs-root",
        type=Path,
        default=Path("/home/kogr/aliro/front_back_optimization"),
    )
    args = parser.parse_args()
    root = args.logs_root

    scenarios = [
        (
            root / "phone_front_human_front/success/logs_human_front_phone_front.log",
            [Segment(0.0, None, True, "PF_HF", max_wrong_pct=20.0)],
        ),
        (
            root / "phone_back_human_front/success/logs_fix_v2.log",
            [Segment(0.0, None, False, "PB_HF success capture", max_wrong_pct=0.0)],
        ),
        (
            root / "phone_back_human_front/fail/logs_fix.log",
            [Segment(0.0, None, False, "PB_HF previous failure", max_wrong_pct=0.0)],
        ),
        (
            root / "pb_hf_at_start.log",
            [Segment(0.0, None, False, "PB_HF cold start", max_wrong_pct=0.0)],
        ),
        (
            root / "pf_hf_then_pb_hf_then_Session_suspend_V4.log",
            [
                Segment(18.672, 33.734, True, "initial PF_HF"),
                Segment(33.734, 47.165, False, "first PB_HF"),
                Segment(47.165, 58.121, True, "second PF_HF"),
                Segment(58.121, None, False, "PB_HF through resume"),
            ],
        ),
    ]

    all_passed = True
    for path, segments in scenarios:
        if not path.exists():
            print(f"{path}: missing")
            all_passed = False
            continue
        all_passed &= evaluate(path, segments)
    return 0 if all_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
