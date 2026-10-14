"""Priority-based task scheduling — paper §6.3, Eq. 8-9, Fig. 7."""
from __future__ import annotations

import dataclasses

from vega.profiling import LatencyModel


def priority(dyn: float, prox: float, age: int, aging_coeff: float = 0.05) -> float:
    """Eq. 8: prio(O) = dyn(O) + prox(O) + delta * age(O)."""
    return dyn + prox + aging_coeff * age


@dataclasses.dataclass
class ObjectState:
    object_id: int
    n_gaussians: int
    dyn: float          # normalized dynamicity level, Eq. 4
    distance: float      # distance from object to viewpoint
    age: int             # frames since this object was last fully computed


Assignment = dict  # object_id -> "cpu" | "gpu" | "skip"


class PriorityScheduler:
    """Greedy solver for the object-selection problem in Eq. 9: maximize
    total priority of objects assigned full computation, subject to a
    per-frame CPU time budget and a per-frame GPU time budget.
    """

    def __init__(self, latency_model: LatencyModel, t_deadline_ms: float, aging_coeff: float = 0.05):
        self.latency_model = latency_model
        self.t_deadline_ms = t_deadline_ms
        self.aging_coeff = aging_coeff

    def _task_time(self, n: int) -> tuple[float, float]:
        """(T_CPU(O), T_GPU(O)) — hash lookup + sort only (Eq. 9's y_CPU/y_GPU
        terms); MLP inference goes to the NPU and isn't part of this budget
        (§6.3: "the NPU rarely reaches its computational limit")."""
        t_cpu = self.latency_model.hash_cpu(n) + self.latency_model.sort_cpu(n)
        t_gpu = self.latency_model.hash_gpu(n) + self.latency_model.sort_gpu(n)
        return t_cpu, t_gpu

    def schedule(self, visible_objects: list[ObjectState]) -> Assignment:
        if not visible_objects:
            return {}
        max_dist = max(o.distance for o in visible_objects) or 1e-6

        scored = []
        for o in visible_objects:
            prox = 1.0 - min(o.distance / max_dist, 1.0)
            p = priority(o.dyn, prox, o.age, self.aging_coeff)
            scored.append((p, o))
        scored.sort(key=lambda x: -x[0])

        remaining_cpu = self.t_deadline_ms
        remaining_gpu = self.t_deadline_ms
        assignment: Assignment = {}

        for _, o in scored:
            t_cpu, t_gpu = self._task_time(o.n_gaussians)
            fits_cpu = t_cpu <= remaining_cpu
            fits_gpu = t_gpu <= remaining_gpu
            if fits_cpu and fits_gpu:
                # "assigned to the processor where the remaining time ratio
                # (available budget / execution time) is smaller"
                ratio_cpu = remaining_cpu / t_cpu if t_cpu > 0 else float("inf")
                ratio_gpu = remaining_gpu / t_gpu if t_gpu > 0 else float("inf")
                if ratio_cpu <= ratio_gpu:
                    assignment[o.object_id] = "cpu"
                    remaining_cpu -= t_cpu
                else:
                    assignment[o.object_id] = "gpu"
                    remaining_gpu -= t_gpu
            elif fits_cpu:
                assignment[o.object_id] = "cpu"
                remaining_cpu -= t_cpu
            elif fits_gpu:
                assignment[o.object_id] = "gpu"
                remaining_gpu -= t_gpu
            else:
                assignment[o.object_id] = "skip"

        return assignment
