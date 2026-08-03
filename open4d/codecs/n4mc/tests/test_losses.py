import torch

from losses.tsdf_losses import compute_rd_loss, narrow_band_loss, sign_consistency_loss, ssim_loss


def test_narrow_band_loss_focuses_on_surface():
    target = torch.tensor([[[[[0.0, 0.8]]]]])
    pred_surface_bad = torch.tensor([[[[[0.3, 0.8]]]]])
    pred_far_bad = torch.tensor([[[[[0.0, 0.5]]]]])

    surface_loss = narrow_band_loss(pred_surface_bad, target, threshold=0.1, mode="hard")
    far_loss = narrow_band_loss(pred_far_bad, target, threshold=0.1, mode="hard")
    assert surface_loss > far_loss


def test_sign_loss_penalizes_wrong_sign():
    target = torch.tensor([[[[[-0.5, 0.5]]]]])
    correct = torch.tensor([[[[[-0.4, 0.4]]]]])
    incorrect = torch.tensor([[[[[0.4, -0.4]]]]])

    assert sign_consistency_loss(incorrect, target) > sign_consistency_loss(correct, target)


def test_ssim_loss_is_lower_for_matching_inputs():
    target = torch.zeros((1, 1, 8, 8, 8), dtype=torch.float32)
    identical = target.clone()
    perturbed = target.clone()
    perturbed[:, :, 2:6, 2:6, 2:6] = 0.5

    assert ssim_loss(identical, target) < ssim_loss(perturbed, target)


def test_compute_rd_loss_handles_mixed_precision_inputs():
    target = torch.zeros((1, 1, 8, 8, 8), dtype=torch.float32)
    outputs = {
        "reconstruction": torch.zeros((1, 1, 8, 8, 8), dtype=torch.float16),
        "rate_bpv": torch.tensor(0.1, dtype=torch.float32),
    }
    total_loss, terms = compute_rd_loss(
        outputs,
        target,
        {
            "reconstruction": "l1",
            "narrow_band_threshold": 0.1,
            "narrow_band_mode": "hard",
            "narrow_band_alpha": 8.0,
            "sign_temperature": 0.1,
            "lambda_rate": 1e-4,
            "lambda_rec": 1.0,
            "lambda_band": 1.0,
            "lambda_sign": 0.1,
            "lambda_ssim": 0.1,
            "ssim_window_size": 5,
            "ssim_sigma": 1.5,
        },
    )
    assert torch.isfinite(total_loss)
    assert "ssim_loss" in terms
