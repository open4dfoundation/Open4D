import torch
import numpy as np


@torch.no_grad()
def quant_motion(res):
    return res.half().float()

@torch.no_grad()
def encode_entropy_motion_npy(res):
    data=res.cpu().numpy()
    data= data.astype(np.float16)

    mask=np.sum(data, axis=-1)!=0
    data=data[mask]

    return data,mask

@torch.no_grad()
def decode_entropy_motion_npy(deform,deform_mask,device):

    rec = np.zeros((deform_mask.shape[0],3))
    rec[deform_mask]=deform

    rec = torch.tensor(rec, device=device).to(torch.float)

    return rec