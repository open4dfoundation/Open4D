"""
Define metrics utilities function.
"""
import torch
import lpips as torch_lpips
from pytorch_msssim import ssim as torch_ssim
import scipy.interpolate
import numpy as np
#


def mse(image_pred, image_gt, valid_mask=None, reduction="mean"):
    """
    Compute mse metrics.

    Args:
        image_pred (torch.Tensor): Image predicted by your algorithm.
        image_gt (torch.Tensor): Ground truth image.
        valid_mask (torch.Tensor, optional): The region of pixel you want to evaluate.
            The default is None, using all pixels.
        reduction (string, optional): reduction method, "mean".

    Returns:
        torch.Tensor: mse value
    """
    value = (image_pred - image_gt) ** 2
    if valid_mask is not None:
        value = value[valid_mask]
    if reduction == "mean":
        return torch.mean(value)
    return value


def mae(image_pred, image_gt, valid_mask=None, reduction="mean"):
    """
    Compute mae metrics.

    Args:
        image_pred (torch.Tensor): Image predicted by your algorithm.
        image_gt (torch.Tensor): Ground truth image.
        valid_mask (torch.Tensor, optional): The region of pixel you want to evaluate.
            The default is None, using all pixels.
        reduction (string, optional): reduction method, "mean".
    Returns:
        torch.Tensor: mae value
    """
    value = torch.abs(image_pred - image_gt)
    if valid_mask is not None:
        value = value[valid_mask]
    if reduction == "mean":
        return torch.mean(value)
    return value


def psnr(image_pred, image_gt, valid_mask=None, reduction="mean"):
    """
    Compute psnr metrics.

    Args:
        image_pred (torch.Tensor): Image predicted by your algorithm.
        image_gt (torch.Tensor): Ground truth image.
        valid_mask (torch.Tensor, optional): The region of pixel you want to evaluate.
            The default is None, using all pixels.
        reduction (string, optional): reduction method, "mean".

    Returns:
        torch.Tensor: psnr value
    """
    return -10 * torch.log10(mse(image_pred, image_gt, valid_mask, reduction))


def ssim(image_pred, image_gt):
    """
    Compute ssim.

    Args:
        image_pred (torch.Tensor): Image predicted by your algorithm.
        image_gt (torch.Tensor): Ground truth image.

    Returns:
        torch.Tensor: ssim value
    """
    image_pred = image_pred.unsqueeze(0)
    image_gt = image_gt.unsqueeze(0)
    ssim_val = torch_ssim(image_pred, image_gt, data_range=1, size_average=False)
    return ssim_val[0]


def lpips(image_pred, image_gt):
    """
    Compute lpips.

    Args:
        image_pred (torch.Tensor): Image predicted by your algorithm.
        image_gt (torch.Tensor): Ground truth image.

    Returns:
        torch.Tensor: lpips value
    """

    loss_fn_alex = torch_lpips.LPIPS(net="alex")  # best forward scores
    image_pred = image_pred.unsqueeze(0)
    image_gt = image_gt.unsqueeze(0)
    return loss_fn_alex(image_pred, image_gt)[0][0][0][0]

def BD_PSNR(R1, PSNR1, R2, PSNR2, piecewise=0):

    '''
    print 'Sample 1'
    R1 = np.array([686.76, 309.58, 157.11, 85.95])
    PSNR1 = np.array([40.28, 37.18, 34.24, 31.42])
    R2 = np.array([893.34, 407.8, 204.93, 112.75])
    PSNR2 = np.array([40.39, 37.21, 34.17, 31.24])

    print 'BD-PSNR: ', BD_PSNR(R1, PSNR1, R2, PSNR2)
    print 'BD-RATE: ', BD_RATE(R1, PSNR1, R2, PSNR2)


    print '\nSample 2'
    R12 = np.array([675.76, 302.58, 151.11, 65.95])
    PSNR12 = np.array([40.18, 36.18, 32.24, 31.02])
    R22 = np.array([883.34, 402.8, 201.93, 102.75])
    PSNR22 = np.array([40.09, 36.21, 32.17, 30.24])

    print 'BD-PSNR: ', BD_PSNR(R12, PSNR12, R22, PSNR22, 1)
    print 'BD-RATE: ', BD_RATE(R12, PSNR12, R22, PSNR22, 1)


    print '\nSample 3'
    print BD_PSNR([686.76, 309.58, 157.11, 85.95],
                  [40.28, 37.18, 34.24, 31.42],
                  [893.34, 407.8, 204.93, 112.75],
                  [40.39, 37.21, 34.17, 31.24])

    '''
    lR1 = np.log(R1)
    lR2 = np.log(R2)

    PSNR1 = np.array(PSNR1)
    PSNR2 = np.array(PSNR2)

    p1 = np.polyfit(lR1, PSNR1, 3)
    p2 = np.polyfit(lR2, PSNR2, 3)

    # integration interval
    min_int = max(min(lR1), min(lR2))
    max_int = min(max(lR1), max(lR2))

    # find integral
    if piecewise == 0:
        p_int1 = np.polyint(p1)
        p_int2 = np.polyint(p2)

        int1 = np.polyval(p_int1, max_int) - np.polyval(p_int1, min_int)
        int2 = np.polyval(p_int2, max_int) - np.polyval(p_int2, min_int)
    else:
        # See https://chromium.googlesource.com/webm/contributor-guide/+/master/scripts/visual_metrics.py
        lin = np.linspace(min_int, max_int, num=100, retstep=True)
        interval = lin[1]
        samples = lin[0]
        v1 = scipy.interpolate.pchip_interpolate(np.sort(lR1), PSNR1[np.argsort(lR1)], samples)
        v2 = scipy.interpolate.pchip_interpolate(np.sort(lR2), PSNR2[np.argsort(lR2)], samples)
        # Calculate the integral using the trapezoid method on the samples.
        int1 = np.trapz(v1, dx=interval)
        int2 = np.trapz(v2, dx=interval)

    # find avg diff
    avg_diff = (int2-int1)/(max_int-min_int)

    return avg_diff