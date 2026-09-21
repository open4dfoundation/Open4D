import numpy as np
import torch
def gen_3d_quant_tbl(constant=1):
    return torch.Tensor(np.load("./codec/quant.npy")*constant).cuda()

def quantize_quality(table,  quality):
    if quality >= 100:
        return torch.ones_like(table)
    factor = 5000 / quality if quality < 50 else 200 - 2 * quality
    return table * factor / 100