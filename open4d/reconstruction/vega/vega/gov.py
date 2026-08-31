"""Group-of-Volumes (GOV) rate-distortion optimization — paper §5.4, Eq. 5-7.

Implements the paper's greedy approximation: rather than actually training
every candidate frame as both a key frame and a residual frame to compare
real costs (prohibitively expensive), the first residual frame in a group is
measured directly, then subsequent frames are decided using a fixed
per-group key-frame cost (`C_key`, computed once) versus a sliding-window
average of the group's realized residual costs (`C_res`) — avoiding
re-running hierarchical color encoding + dynamicity filtering on every
candidate frame just to test it.

A note on the switch-over direction: the paper's prose states "If C_key is
larger than C_res, the frame is set as a new key frame; otherwise it is set
as a residual frame." Taken completely literally that produces a degenerate
planner: C_res starts *below* C_key by construction right after a key frame
(residual frames are supposed to be cheap — that's the entire point of the
GOV structure), so "C_key > C_res" would already hold at the very first
candidate frame and every group would collapse to length 1. Combined with
the paper's own statement that residual distortion grows the farther a
frame gets from its key frame, the coherent RD-minimizing reading is the
reverse: keep extending the group while the (rising) residual cost stays
below the key-frame cost, and cut a new group once the estimated residual
cost *exceeds* it. That is the direction implemented below (see
`GOVPlanner.should_start_new_key`); this is a deliberate, documented
interpretation of a likely inversion in the paper's wording, not an
oversight.
"""
from __future__ import annotations

import dataclasses
from typing import Callable

DEFAULT_LAMBDA = 0.003  # paper §5.4


def rd_cost(rate_bytes: float, distortion: float, lam: float = DEFAULT_LAMBDA) -> float:
    """Eq. 5: C(i) = R(i) + lambda * D(i)."""
    return rate_bytes + lam * distortion


@dataclasses.dataclass
class FrameCost:
    frame_type: str  # "key" or "residual"
    rate_bytes: float
    distortion: float
    cost: float


class GOVPlanner:
    """Tracks per-group state needed to make the greedy key/residual
    decision described in §5.4."""

    def __init__(self, lam: float = DEFAULT_LAMBDA, window_size: int = 5,
                 max_group_len: int | None = None):
        self.lam = lam
        self.window_size = window_size
        # Upper bound on frames per GOV, independent of the RD rule. The RD
        # test only fires once the *estimated* residual cost overtakes the key
        # cost, which on a slow-moving subject may never happen -- leaving one
        # key frame for the whole sequence and letting residual drift grow
        # without bound (basketball: a ball held still for ~12 frames then
        # raised overhead dissolves entirely by frame 24). This is the max-GOP
        # refresh a video codec would apply: it trades bitrate for a hard
        # ceiling on drift. None disables it, preserving pure-RD behaviour.
        self.max_group_len = max_group_len
        self.group_key_cost: float | None = None
        self.res_costs_in_group: list[float] = []

    def start_new_group(self, key_rate: float, key_distortion: float) -> FrameCost:
        cost = rd_cost(key_rate, key_distortion, self.lam)
        self.group_key_cost = cost
        self.res_costs_in_group = []
        return FrameCost("key", key_rate, key_distortion, cost)

    def estimated_res_cost(self) -> float | None:
        if not self.res_costs_in_group:
            return None
        window = self.res_costs_in_group[-self.window_size:]
        return sum(window) / len(window)

    def group_len(self) -> int:
        """Frames committed to the current group, counting its key frame."""
        return 1 + len(self.res_costs_in_group)

    def should_start_new_key(self) -> bool:
        """Whether the *next* candidate frame should become a new key frame
        rather than another residual frame in the current group."""
        # Checked first and unconditionally: the cap has to hold even when the
        # RD estimate is unavailable or still favours extending the group.
        if self.max_group_len is not None and self.group_len() >= self.max_group_len:
            return True
        est = self.estimated_res_cost()
        if est is None or self.group_key_cost is None:
            return False
        return est > self.group_key_cost

    def record_residual(self, rate_bytes: float, distortion: float) -> FrameCost:
        cost = rd_cost(rate_bytes, distortion, self.lam)
        self.res_costs_in_group.append(cost)
        return FrameCost("residual", rate_bytes, distortion, cost)


def plan_and_encode(
    n_frames: int,
    encode_key: Callable[[int], tuple[float, float, object]],
    encode_residual: Callable[[int], tuple[float, float, object]],
    lam: float = DEFAULT_LAMBDA,
    window_size: int = 5,
    max_group_len: int | None = None,
):
    """Drive the GOV planner over a sequence of `n_frames`.

    `encode_key(i)` / `encode_residual(i)` perform the actual (side-effecting)
    encoding work for frame `i` and return `(rate_bytes, distortion,
    artifact)`; `artifact` is opaque to this module (e.g. the encoded
    per-object data for that frame) and is just passed back to the caller.

    Frame 0 is always a key frame and frame 1 is always a residual frame
    (§5.4 assumption #1); subsequent frames follow the greedy RD rule.

    Returns:
        frame_costs: list[FrameCost]
        artifacts: list — one per frame, as returned by the encode_* callbacks
        group_ids: list[int] — which GOV group each frame belongs to
    """
    assert n_frames >= 1
    planner = GOVPlanner(lam=lam, window_size=window_size,
                         max_group_len=max_group_len)
    frame_costs: list[FrameCost] = []
    artifacts: list = []
    group_ids: list[int] = []
    current_group = -1

    for i in range(n_frames):
        make_key = (i == 0) or (i >= 2 and planner.should_start_new_key())
        if i == 1:
            make_key = False  # assumption #1: second frame is always residual

        if make_key:
            rate, dist, artifact = encode_key(i)
            fc = planner.start_new_group(rate, dist)
            current_group += 1
        else:
            rate, dist, artifact = encode_residual(i)
            fc = planner.record_residual(rate, dist)

        frame_costs.append(fc)
        artifacts.append(artifact)
        group_ids.append(current_group)

    return frame_costs, artifacts, group_ids
