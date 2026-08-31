from vega.profiling import FittedLatency, LatencyModel
from vega.scheduler import ObjectState, PriorityScheduler, priority


def test_priority_formula():
    # Eq. 8: prio(O) = dyn(O) + prox(O) + delta * age(O)
    assert priority(dyn=0.5, prox=0.5, age=0, aging_coeff=0.05) == 1.0
    assert priority(dyn=0.0, prox=0.0, age=10, aging_coeff=0.05) == 0.5


def _flat_model(cost_per_object_ms: float) -> LatencyModel:
    zero = FittedLatency(0.0, 0.0)
    flat = FittedLatency(0.0, cost_per_object_ms)
    return LatencyModel(hash_cpu=flat, hash_gpu=flat, mlp_gpu=zero,
                        sort_cpu=zero, sort_gpu=zero, render_gpu=zero)


def test_high_priority_objects_scheduled_first_under_tight_deadline():
    model = _flat_model(cost_per_object_ms=10.0)  # each object costs 10ms on cpu or gpu
    sched = PriorityScheduler(model, t_deadline_ms=15.0)  # only enough budget for ~1 object per processor
    states = [
        ObjectState(object_id=0, n_gaussians=100, dyn=0.9, distance=1.0, age=0),  # high priority
        ObjectState(object_id=1, n_gaussians=100, dyn=0.1, distance=5.0, age=0),  # low priority
        ObjectState(object_id=2, n_gaussians=100, dyn=0.05, distance=5.0, age=0),  # low priority
    ]
    assignment = sched.schedule(states)
    assert assignment[0] in ("cpu", "gpu")  # highest priority object must be scheduled, not skipped
    n_skipped = sum(1 for v in assignment.values() if v == "skip")
    assert n_skipped >= 1  # budget can't fit all three


def test_all_objects_fit_when_deadline_generous():
    model = _flat_model(cost_per_object_ms=1.0)
    sched = PriorityScheduler(model, t_deadline_ms=1000.0)
    states = [ObjectState(object_id=i, n_gaussians=10, dyn=0.5, distance=1.0, age=0) for i in range(5)]
    assignment = sched.schedule(states)
    assert all(v != "skip" for v in assignment.values())


def test_aging_eventually_prioritizes_starved_object():
    model = _flat_model(cost_per_object_ms=10.0)
    sched = PriorityScheduler(model, t_deadline_ms=15.0, aging_coeff=0.05)
    states = [
        ObjectState(object_id=0, n_gaussians=100, dyn=0.9, distance=1.0, age=0),
        ObjectState(object_id=1, n_gaussians=100, dyn=0.0, distance=10.0, age=1000),  # long-starved
    ]
    assignment = sched.schedule(states)
    # object 1's aging bonus (0.05*1000=50) now dwarfs object 0's priority (~1.9),
    # so it should be scheduled ahead and not skipped.
    assert assignment[1] in ("cpu", "gpu")
