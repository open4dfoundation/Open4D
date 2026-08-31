from vega.gov import GOVPlanner, plan_and_encode, rd_cost


def test_rd_cost_formula():
    assert rd_cost(rate_bytes=1000, distortion=0, lam=0.003) == 1000
    assert abs(rd_cost(rate_bytes=1000, distortion=100, lam=0.003) - 1000.3) < 1e-9


def test_frame0_key_frame1_residual():
    calls = []

    def encode_key(i):
        calls.append(("key", i))
        return 1000.0, 0.0, f"key{i}"

    def encode_residual(i):
        calls.append(("res", i))
        return 10.0, 5.0, f"res{i}"

    costs, artifacts, groups = plan_and_encode(5, encode_key, encode_residual)
    assert costs[0].frame_type == "key"
    assert costs[1].frame_type == "residual"
    assert artifacts[0] == "key0"
    assert artifacts[1] == "res1"


def test_group_switches_when_residual_cost_exceeds_key_cost():
    # Residual cost spikes at frame 3; the *decision* to start a new key
    # frame is made from the sliding-window average of costs seen so far
    # (i.e. it lags one frame behind the spike itself), so the new group
    # starts at frame 4, not frame 3.
    residual_costs = iter([10.0, 20.0, 5000.0, 10.0, 20.0])

    def encode_key(i):
        return 1000.0, 0.0, f"key{i}"

    def encode_residual(i):
        r = next(residual_costs)
        return r, 0.0, f"res{i}"

    costs, artifacts, groups = plan_and_encode(5, encode_key, encode_residual, window_size=2)
    types = [c.frame_type for c in costs]
    assert types == ["key", "residual", "residual", "residual", "key"]
    assert groups == [0, 0, 0, 0, 1]


def test_planner_sliding_window_average():
    p = GOVPlanner(lam=0.003, window_size=2)
    p.start_new_group(key_rate=100.0, key_distortion=0.0)
    assert p.should_start_new_key() is False  # no residual history yet
    p.record_residual(rate_bytes=10.0, distortion=0.0)
    p.record_residual(rate_bytes=20.0, distortion=0.0)
    # window=2 average of [10,20] = 15, well under key cost 100
    assert p.estimated_res_cost() == 15.0
    assert p.should_start_new_key() is False
    p.record_residual(rate_bytes=1000.0, distortion=0.0)
    # window=2 average of [20,1000] = 510 > 100
    assert p.should_start_new_key() is True
