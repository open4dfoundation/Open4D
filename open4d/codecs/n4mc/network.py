import os
import re
import glob
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.nn import init
from PIL import Image
from torch.utils.data import Dataset
from torch import Tensor
from torch.nn.parameter import Parameter, UninitializedParameter
import math
from tqdm import tqdm
from quantize_utils import QuantConv3d,QuantLinear,QuantConvTranspose3d
#from glob import glob
from timm.models.layers import trunc_normal_, DropPath
from typing import Tuple

def ActivationLayer(act_type):
    if act_type == 'relu':
        act_layer = nn.ReLU(True)
    elif act_type == 'leaky':
        act_layer = nn.LeakyReLU(inplace=True)
    elif act_type == 'leaky01':
        act_layer = nn.LeakyReLU(negative_slope=0.1, inplace=True)
    elif act_type == 'relu6':
        act_layer = nn.ReLU6(inplace=True)
    elif act_type == 'gelu':
        act_layer = nn.GELU()
    elif act_type == 'sin':
        act_layer = torch.sin
    elif act_type == 'swish':
        act_layer = nn.SiLU(inplace=True)
    elif act_type == 'softplus':
        act_layer = nn.Softplus()
    elif act_type == 'hardswish':
        act_layer = nn.Hardswish(inplace=True)
    else:
        raise KeyError(f"Unknown activation function {act_type}.")

    return act_layer

class PixelShuffle3D(nn.Module):
    def __init__(self, upscale_factor):
        super(PixelShuffle3D, self).__init__()
        self.upscale_factor = upscale_factor

    def forward(self, x):
        batch_size, channels, depth, height, width = x.size()
        
        # Reshape to prepare for pixel shuffle
        x = x.view(batch_size, channels // self.upscale_factor ** 3, self.upscale_factor, self.upscale_factor, self.upscale_factor, depth, height, width)
        
        # Permute dimensions for pixel shuffle
        x = x.permute(0, 1, 5, 2, 6, 3, 7, 4).contiguous()
        
        # Reshape to get the final result
        x = x.view(batch_size, channels // self.upscale_factor ** 3, depth * self.upscale_factor, height * self.upscale_factor, width * self.upscale_factor)
        
        return x

"""class CustomConv(nn.Module):
    def __init__(self, **kargs):
        super(CustomConv, self).__init__()

        ngf, new_ngf, stride = kargs['ngf'], kargs['new_ngf'], kargs['stride']
        self.conv_type = kargs['conv_type']
        if self.conv_type == 'conv':
            self.conv = nn.Conv3d(ngf, new_ngf * stride * stride * stride, 3, 1, 1, bias=kargs['bias'])
            self.up_scale =  PixelShuffle3D(stride)         #nn.PixelShuffle(stride)
        elif self.conv_type == 'deconv':
            self.conv = nn.ConvTranspose3d(ngf, new_ngf, stride, stride)
            self.up_scale = nn.Identity()
        elif self.conv_type == 'bilinear':
            self.conv = nn.Upsample(scale_factor=stride, mode='bilinear', align_corners=True)
            self.up_scale = nn.Conv3d(ngf, new_ngf, 2*stride+1, 1, stride, bias=kargs['bias'])

    def forward(self, x):
        out = self.conv(x)
        return self.up_scale(out)"""
    

class QuantSepConv(nn.Module):
    def __init__(self,in_channel, out_channel, kernel_size, num_bits,bias=True):
        super().__init__()

        self.conv1=QuantConv3d(in_channel,out_channel//3,(kernel_size,1,1),1,(kernel_size//2,0,0),bias=bias,num_bits=num_bits)
        self.conv2=QuantConv3d(in_channel,out_channel//3,(1,kernel_size,1),1,(0,kernel_size//2,0),bias=bias,num_bits=num_bits)
        self.conv3=QuantConv3d(in_channel,out_channel//3,(1,1,kernel_size),1,(0,0,kernel_size//2),bias=bias,num_bits=num_bits)
        self.conv=QuantConv3d(out_channel//3*3, out_channel,1,1,0,bias=bias,num_bits=num_bits)

    def forward(self, input):
        feature1=self.conv1(input)
        feature2=self.conv2(input)
        feature3=self.conv3(input)

        output=self.conv(torch.cat([feature1,feature2,feature3],dim=1))
        return output


class QuantCustomConv(nn.Module):
    def __init__(self, **kargs):
        super(QuantCustomConv, self).__init__()

        ngf, new_ngf, stride,num_bits = kargs['ngf'], kargs['new_ngf'], kargs['stride'], kargs['num_bits']
        self.conv_type = kargs['conv_type']
        if self.conv_type == 'conv':
            self.conv = QuantConv3d(ngf, new_ngf * stride * stride * stride, 3, 1, 1, bias=kargs['bias'],num_bits=num_bits)
            self.up_scale =  PixelShuffle3D(stride)         #nn.PixelShuffle(stride)
        elif self.conv_type == 'conv2':
            self.conv = QuantConv3d(ngf, new_ngf//4 * stride * stride * stride, 3, 1, 1, bias=kargs['bias'],num_bits=num_bits)
            self.up_scale =  nn.Sequential(PixelShuffle3D(stride), QuantConv3d(new_ngf//4, new_ngf, 3, 1, 1, bias=kargs['bias'],num_bits=num_bits))         #nn.PixelShuffle(stride)
        elif self.conv_type== 'sepconv':
            self.conv = QuantSepConv(ngf, new_ngf * stride * stride * stride, 3, num_bits=num_bits, bias=kargs['bias'])
            self.up_scale =  PixelShuffle3D(stride)         #nn.PixelShuffle(stride)
        elif self.conv_type == 'deconv':
            self.conv = QuantConvTranspose3d(ngf, new_ngf, stride, stride, num_bits=num_bits)
            self.up_scale = nn.Identity()
        elif self.conv_type == 'bilinear':
            self.conv = nn.Upsample(scale_factor=stride, mode='trilinear', align_corners=True)
            #self.up_scale = QuantConv3d(ngf, new_ngf, 2*stride+1, 1, stride, bias=kargs['bias'],num_bits=num_bits)
            self.up_scale = QuantConv3d(ngf, new_ngf, 3, 1, 1, bias=kargs['bias'],num_bits=num_bits)
        else:
            print('no such conv type')
            assert False

    def forward(self, x):
        out = self.conv(x)
        return self.up_scale(out)

def MLP(dim_list, act='relu', bias=True):
    act_fn = ActivationLayer(act)
    fc_list = []
    for i in range(len(dim_list) - 1):
        fc_list += [nn.Linear(dim_list[i], dim_list[i+1], bias=bias), act_fn]
    return nn.Sequential(*fc_list)

def QuantMLP(dim_list, act='relu', bias=True,num_bits=8):
    act_fn = ActivationLayer(act)
    fc_list = []
    for i in range(len(dim_list) - 1):
        fc_list += [QuantLinear(dim_list[i], dim_list[i+1], bias=bias,num_bits=num_bits), act_fn]
    return nn.Sequential(*fc_list)

class PositionalEncoding(nn.Module):
    def __init__(self, lbase=1.25,levels=40):
        super(PositionalEncoding, self).__init__()
        """self.pe_embed = pe_embed.lower()
        if self.pe_embed == 'none':
            self.embed_length = 1
        else:
            self.lbase, self.levels = [float(x) for x in pe_embed.split('_')]
            self.levels = int(self.levels)
            self.embed_length = 2 * self.levels"""

        self.lbase=lbase
        self.levels=levels
        self.embed_length = 2 * self.levels

    def forward(self, pos):
        
        pe_list = []
        for i in range(self.levels):
            temp_value = pos * self.lbase **(i) * math.pi
            pe_list += [torch.sin(temp_value), torch.cos(temp_value)]
        return torch.stack(pe_list, 1).squeeze(-1)

"""class NeRVBlock3D(nn.Module):
    def __init__(self, in_channel,out_channel,scale,bias,act,conv_type):
        super().__init__()

        self.conv = CustomConv(ngf=in_channel, new_ngf=out_channel, stride=scale, bias=bias, 
            conv_type=conv_type)
        self.act = ActivationLayer(act)

    def forward(self, x):
        return self.act(self.conv(x))"""
    
class QuantNeRVBlock3D(nn.Module):
    def __init__(self, in_channel,out_channel,scale,bias,act,conv_type,num_bits):
        super().__init__()

        self.conv = QuantCustomConv(ngf=in_channel, new_ngf=out_channel, stride=scale, bias=bias, 
            conv_type=conv_type,num_bits=num_bits)
        self.act = ActivationLayer(act)

    def forward(self, x):
        return self.act(self.conv(x))


class STEQuantize(torch.autograd.Function):
  """Straight-Through Estimator for Quantization.

  Forward pass implements quantization by rounding to integers,
  backward pass is set to gradients of the identity function.
  """
  @staticmethod
  def forward(ctx, x):
    ctx.save_for_backward(x)
    return x.round()

  @staticmethod
  def backward(ctx, grad_outputs):
    return grad_outputs
  
'''
def diff_quantized_tensor(input,num_bits=8,min=-1,max=1):
    quant=STEQuantize.apply
    scale=(max - min) / (2**num_bits)
    input=torch.clamp(input,min,max)
    quanted_tensor=quant((input-min)/(scale))*scale+min
    #quanted_tensor=torch.clamp(quanted_tensor,min,max)
    return quanted_tensor
'''
def diff_quantized_tensor(input, num_bits=8, min_val=-1.0, max_val=1.0, per_channel=False, eps=1e-6):
    """
    Safer fake-quantization with STE.
    - Uses levels = 2**num_bits - 1
    - Clamps scale to eps
    - Clamps output to [min_val, max_val]
    - Supports per-channel quantization (per output channel)
    """
    if input is None:
        return None
    if num_bits is None or int(num_bits) >= 32:
        return input

    quant = STEQuantize.apply
    levels = float(2**num_bits - 1)
    x = input

    if per_channel and x.dim() >= 2:
        # Compute per-out-channel stats
        x_min = x.view(x.size(0), -1).amin(dim=1, keepdim=True)
        x_max = x.view(x.size(0), -1).amax(dim=1, keepdim=True)

        # Make shapes broadcastable
        x_min = x_min.view(-1, *([1] * (x.dim() - 1)))
        x_max = x_max.view(-1, *([1] * (x.dim() - 1)))

        # Clamp with numeric constants, not tensors
        x_min = torch.clamp(x_min, min=min_val, max=max_val)
        x_max = torch.clamp(x_max, min=min_val, max=max_val)

        scale = (x_max - x_min) / levels
        scale = torch.clamp(scale, min=eps)
        normalized = (x - x_min) / scale
        q = quant(normalized)
        deq = q * scale + x_min
        deq = torch.clamp(deq, min_val, max_val)
        return deq

    # Per-tensor path
    x = torch.clamp(x, min_val, max_val)
    scale = (max_val - min_val) / levels
    scale = max(scale, eps)
    normalized = (x - min_val) / scale
    q = quant(normalized)
    deq = q * scale + min_val
    deq = torch.clamp(deq, min_val, max_val)
    return deq




def adjust_lr(optimizer, cur_epoch, cur_iter, data_size, args):
    cur_epoch = cur_epoch + (float(cur_iter) / data_size)
    if args.lr_type == 'cosine':
        lr_mult = 0.5 * (math.cos(math.pi * (cur_epoch - int(args.warmup*args.n_epoch))/ (args.n_epoch - int(args.warmup*args.n_epoch))) + 1.0)
    elif args.lr_type == 'step':
        lr_mult = 0.1 ** (sum(cur_epoch >= np.array(args.lr_steps)))
    elif args.lr_type == 'const':
        lr_mult = 1
    elif args.lr_type == 'plateau':
        lr_mult = 1
    else:
        raise NotImplementedError

    if cur_epoch < int(args.warmup*args.n_epoch):
        lr_mult = 0.1 + 0.9 * cur_epoch / int(args.warmup*args.n_epoch)

    for i, param_group in enumerate(optimizer.param_groups):
        param_group['lr'] = args.lr * lr_mult

    return args.lr * lr_mult


#---------------convnext-----------

class Block3D(nn.Module):
    r""" ConvNeXt Block. There are two equivalent implementations:
    (1) DwConv -> LayerNorm (channels_first) -> 1x1 Conv -> GELU -> 1x1 Conv; all in (N, C, H, W)
    (2) DwConv -> Permute to (N, H, W, C); LayerNorm (channels_last) -> Linear -> GELU -> Linear; Permute back
    We use (2) as we find it slightly faster in PyTorch
    
    Args:
        dim (int): Number of input channels.
        drop_path (float): Stochastic depth rate. Default: 0.0
        layer_scale_init_value (float): Init value for Layer Scale. Default: 1e-6.
    """
    def __init__(self, dim, drop_path=0., layer_scale_init_value=1e-6):
        super().__init__()
        self.dwconv = nn.Conv3d(dim, dim, kernel_size=7, padding=3, groups=dim) # depthwise conv
        self.norm = LayerNorm(dim, eps=1e-6)
        self.pwconv1 = nn.Linear(dim, 4 * dim) # pointwise/1x1 convs, implemented with linear layers
        self.act = nn.GELU()
        self.pwconv2 = nn.Linear(4 * dim, dim)
        self.gamma = nn.Parameter(layer_scale_init_value * torch.ones((dim)), 
                                    requires_grad=True) if layer_scale_init_value > 0 else None
        self.drop_path = DropPath(drop_path) if drop_path > 0. else nn.Identity()

    def forward(self, x):
        input = x
        x = self.dwconv(x)
        x = x.permute(0, 2, 3, 4, 1) # (N, C, H, W, D) -> (N, H, W, D, C)
        x = self.norm(x)
        x = self.pwconv1(x)
        x = self.act(x)
        x = self.pwconv2(x)
        if self.gamma is not None:
            x = self.gamma * x
        x = x.permute(0, 4, 1, 2, 3) # (N, H, W, D, C) -> (N, C, H, W, D)

        x = input + self.drop_path(x)
        return x


class ConvNeXt3D(nn.Module):
    r""" ConvNeXt
        A PyTorch impl of : `A ConvNet for the 2020s`  -
          https://arxiv.org/pdf/2201.03545.pdf

    Args:
        in_chans (int): Number of input image channels. Default: 3
        num_classes (int): Number of classes for classification head. Default: 1000
        depths (tuple(int)): Number of blocks at each stage. Default: [3, 3, 9, 3]
        dims (int): Feature dimension at each stage. Default: [96, 192, 384, 768]
        drop_path_rate (float): Stochastic depth rate. Default: 0.
        layer_scale_init_value (float): Init value for Layer Scale. Default: 1e-6.
        head_init_scale (float): Init scaling value for classifier weights and biases. Default: 1.
    """
    def __init__(self, stage_blocks=0, strds=[2,2,2,2], dims=[96, 192, 384, 768], 
            in_chans=3, drop_path_rate=0., layer_scale_init_value=1e-6,
                 ):
        super().__init__()

        self.downsample_layers = nn.ModuleList() # stem and 3 intermediate downsampling conv layers
        self.stages = nn.ModuleList() # 4 feature resolution stages, each consisting of multiple residual blocks
        self.stage_num = len(dims)
        dp_rates=[x.item() for x in torch.linspace(0, drop_path_rate, stage_blocks*self.stage_num)] 
        cur = 0
        for i in range(self.stage_num):
            # Build downsample layers
            if i > 0:
                downsample_layer = nn.Sequential(
                        LayerNorm3D(dims[i-1], eps=1e-6, data_format="channels_first"),
                        nn.Conv3d(dims[i-1], dims[i], kernel_size=strds[i], stride=strds[i]),
                )
            else:
                downsample_layer = nn.Sequential(
                    nn.Conv3d(in_chans, dims[0], kernel_size=strds[i], stride=strds[i]),
                    LayerNorm3D(dims[0], eps=1e-6, data_format="channels_first")
                )                
            self.downsample_layers.append(downsample_layer)

            # Build more blocks
            stage = nn.Sequential(
                *[Block3D(dim=dims[i], drop_path=dp_rates[cur + j], 
                layer_scale_init_value=layer_scale_init_value) for j in range(stage_blocks)]
            )
            self.stages.append(stage)
            cur += stage_blocks

        self.apply(self._init_weights)

    def _init_weights(self, m):
        if isinstance(m, (nn.Conv3d, nn.Linear)):
            trunc_normal_(m.weight, std=.02)
            nn.init.constant_(m.bias, 0)

    def forward(self, x):
        out_list = []
        for i in range(self.stage_num):
            x = self.downsample_layers[i](x)
            x = self.stages[i](x)
            out_list.append(x)
        return out_list[-1]


class LayerNorm(nn.Module):
    r""" LayerNorm that supports two data formats: channels_last (default) or channels_first. 
    The ordering of the dimensions in the inputs. channels_last corresponds to inputs with 
    shape (batch_size, height, width, channels) while channels_first corresponds to inputs 
    with shape (batch_size, channels, height, width).
    """
    def __init__(self, normalized_shape, eps=1e-6, data_format="channels_last"):
        super().__init__()
        self.weight = nn.Parameter(torch.ones(normalized_shape))
        self.bias = nn.Parameter(torch.zeros(normalized_shape))
        self.eps = eps
        self.data_format = data_format
        if self.data_format not in ["channels_last", "channels_first"]:
            raise NotImplementedError 
        self.normalized_shape = (normalized_shape, )
    
    def forward(self, x):
        if self.data_format == "channels_last":
            return F.layer_norm(x, self.normalized_shape, self.weight, self.bias, self.eps)
        elif self.data_format == "channels_first":
            u = x.mean(1, keepdim=True)
            s = (x - u).pow(2).mean(1, keepdim=True)
            x = (x - u) / torch.sqrt(s + self.eps)
            x = self.weight[:, None, None] * x + self.bias[:, None, None]
            return x


class LayerNorm3D(nn.Module):
    r""" LayerNorm that supports two data formats: channels_last (default) or channels_first. 
    The ordering of the dimensions in the inputs. channels_last corresponds to inputs with 
    shape (batch_size, height, width, channels) while channels_first corresponds to inputs 
    with shape (batch_size, channels, height, width, depth).
    """
    def __init__(self, normalized_shape, eps=1e-6, data_format="channels_last"):
        super().__init__()
        self.weight = nn.Parameter(torch.ones(normalized_shape))
        self.bias = nn.Parameter(torch.zeros(normalized_shape))
        self.eps = eps
        self.data_format = data_format
        if self.data_format not in ["channels_last", "channels_first"]:
            raise NotImplementedError 
        self.normalized_shape = (normalized_shape, )
    
    def forward(self, x):
        if self.data_format == "channels_last":
            return F.layer_norm(x, self.normalized_shape, self.weight, self.bias, self.eps)
        elif self.data_format == "channels_first":
            u = x.mean(1, keepdim=True)
            s = (x - u).pow(2).mean(1, keepdim=True)
            x = (x - u) / torch.sqrt(s + self.eps)
            x = self.weight[:, None, None, None] * x + self.bias[:, None, None, None]
            return x



class QuantGeneratorV2(nn.Module):
    def __init__(self, args):
        super().__init__()
        self.args = args

        encoder_dim_list = [int(x) for x in args.encoder_dim_list.split('_')]
        encoder_stride_list = [int(x) for x in args.encoder_stride_list.split('_')]

        decoder_dim_list = [int(x) for x in args.decoder_dim_list.split('_')]
        decoder_stride_list = [int(x) for x in args.decoder_stride_list.split('_')]

        bias = args.bias
        act = args.act
        conv_type = args.conv_type
        num_bits = args.num_bits
        self.num_bits = num_bits

        # latent config (explicit)
        self.in_chans = 1
        self.embed_dim = int(args.embed_dim)
        self.embed_hwd = int(args.embed_hwd)

        # Encoder (keeps spatial structure)
        self.encoder = ConvNeXt3D(
            stage_blocks=1,
            strds=encoder_stride_list,
            dims=encoder_dim_list,
            in_chans=self.in_chans,
            drop_path_rate=0
        )
        encoder_out_ch = encoder_dim_list[-1]

        # Projection: spatial-resize encoder feature map -> (embed_hwd,embed_hwd,embed_hwd) then conv -> embed_dim
        # We use a 1x1x1 conv (can be quantized) to change channels to embed_dim.
        # Keep this projection lightweight so encoder still governs expressiveness.
        # Use QuantConv3d if you want the projection to be quantized as part of the decoder pipeline.
        # Here we provide both choices depending on args.use_quant_proj (default: False)
        use_quant_proj = getattr(args, "use_quant_proj", True)
        if use_quant_proj:
            self.proj_conv = QuantConv3d(encoder_out_ch, self.embed_dim, kernel_size=1, stride=1, padding=0, bias=bias, num_bits=num_bits)
        else:
            self.proj_conv = nn.Conv3d(encoder_out_ch, self.embed_dim, kernel_size=1, stride=1, padding=0, bias=bias)

        # Decoder: starts from (B, embed_dim, embed_hwd, embed_hwd, embed_hwd)
        decoder_layers_list = []
        for i in range(len(decoder_dim_list)):
            in_channel = self.embed_dim if i == 0 else decoder_dim_list[i-1]
            out_channel = decoder_dim_list[i]
            scale = decoder_stride_list[i]
            decoder_layers_list.append(
                QuantNeRVBlock3D(in_channel, out_channel, scale, bias, act, conv_type, num_bits=num_bits)
            )
        decoder_layers_list.append(
            QuantConv3d(decoder_dim_list[-1], 1, 3, 1, 1, bias=bias, num_bits=num_bits)
        )
        self.decoder = nn.Sequential(*decoder_layers_list)

    def forward(self, input_voxel=None, embed_features=None, bypass_quant_proj=False):
        """
        If input_voxel is provided: return embed_features shaped (B, C, D, H, W).
        If embed_features is provided: decode (expects shape (B, C, D, H, W)).
        Backward compatibility:
            also accepts/returns channels-last tensors if inputs are channels-last.
        bypass_quant_proj: if True, skip quantizing the projection conv (useful during training).
        """

        if input_voxel is not None:
            is_channels_last = (
                input_voxel.dim() == 5
                and input_voxel.shape[-1] == self.in_chans
                and input_voxel.shape[1] != self.in_chans
            )
            x = input_voxel.permute(0, 4, 1, 2, 3) if is_channels_last else input_voxel
            enc = self.encoder(x)  # (B, C_enc, D_e, H_e, W_e)

            target_hwd = (self.embed_hwd, self.embed_hwd, self.embed_hwd)
            if enc.shape[-3:] != target_hwd:
                enc = F.interpolate(enc, size=target_hwd, mode='trilinear', align_corners=False)

            proj = self.proj_conv(enc)  # (B, embed_dim, embed_hwd, embed_hwd, embed_hwd)
            return proj.permute(0, 2, 3, 4, 1) if is_channels_last else proj

        if embed_features is None:
            raise ValueError("Either input_voxel or embed_features must be provided.")

        is_channels_last = (
            embed_features.dim() == 5
            and embed_features.shape[-1] == self.embed_dim
            and embed_features.shape[1] != self.embed_dim
        )
        embed = embed_features.permute(0, 4, 1, 2, 3) if is_channels_last else embed_features
        pred = self.decoder(embed)  # (B, 1, D_out, H_out, W_out)
        return pred.permute(0, 2, 3, 4, 1) if is_channels_last else pred

    # --- helpers unchanged (you can keep your quant save/load functions) ---
    def get_encoder_params(self):
        all_params = []
        for param in self.encoder.parameters():
            all_params.append(param.reshape(-1))
        all_params = torch.cat(all_params, dim=0)
        return torch.mean(all_params)

    def get_decoder_quantparams(self):
        all_params = []
        for param in self.decoder.parameters():
            all_params.append(diff_quantized_tensor(param.reshape(-1), self.num_bits))
        all_params = torch.cat(all_params, dim=0)
        return torch.mean(all_params)

    def save_encoder_weights(self, save_path):
        ori_weight_dict = self.encoder.state_dict()
        torch.save(ori_weight_dict, save_path)

    def save_decoder_weights(self, save_path):
        ori_weight_dict = self.decoder.state_dict()
        torch.save(ori_weight_dict, save_path)

    def save_quanted_encoder_weights(self, save_path):
        ori_weight_dict = self.encoder.state_dict()
        quanted_weight_dict = {}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key] = diff_quantized_tensor(ori_weight_dict[key], self.num_bits)
        torch.save(quanted_weight_dict, save_path)

    def save_quanted_decoder_weights(self, save_path):
        ori_weight_dict = self.decoder.state_dict()
        quanted_weight_dict = {}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key] = diff_quantized_tensor(ori_weight_dict[key], self.num_bits)
        torch.save(quanted_weight_dict, save_path)

def get_network(name,args):
    if name == "QuantGeneratorV2":
        return QuantGeneratorV2(args)
    else:
        print('no selected model !!!')
        assert False



class QuantDecoder(nn.Module):
    def __init__(self,args):
        super().__init__()
        self.args=args
        decoder_dim_list=[int(decoder_dim) for decoder_dim in args.decoder_dim_list.split('_')]
        decoder_stride_list=[int(decoder_stride) for decoder_stride in args.decoder_stride_list.split('_')]

        embed_dim=args.embed_dim

        bias = args.bias             
        act=args.act                      
        conv_type=args.conv_type   

        after_embed_dim=args.after_embed_dim        

        num_bits=args.num_bits
        self.num_bits=args.num_bits

        decoder_layers_list=[]
        if after_embed_dim>0:
            decoder_layers_list.append(QuantConv3d(embed_dim,after_embed_dim,1,1,bias=bias,num_bits=num_bits))
        else:
            after_embed_dim=embed_dim

        for i in range(len(decoder_dim_list)):
            if i==0:
                in_channel=after_embed_dim
                out_channel=decoder_dim_list[i]
                scale=decoder_stride_list[i]
            else:
                in_channel=decoder_dim_list[i-1]
                out_channel=decoder_dim_list[i]
                scale=decoder_stride_list[i]
            decoder_layers_list.append(QuantNeRVBlock3D(in_channel,out_channel,scale,bias,act,conv_type,num_bits=num_bits))

        decoder_layers_list.append( QuantConv3d(decoder_dim_list[-1],4,3,1,1,bias=bias,num_bits=num_bits))
        self.decoder_layers=nn.Sequential(*decoder_layers_list)

    def forward(self,embed_features):
        #embed_features:    B,N,N,N,C
        embed_features=embed_features.permute(0,4,1,2,3)
        pred_voxel=self.decoder_layers(embed_features)  #(B,C,N,N,N)
        return pred_voxel.permute(0,2,3,4,1)        #(B,N,N,N,C)

    def get_quantparams(self):
        all_params=[]
        for param in self.parameters():
            all_params.append(diff_quantized_tensor(param.reshape(-1),self.num_bits))
        all_params=torch.cat(all_params,dim=0)
        return torch.mean(all_params) 
    
    def save_quanted_decoder_weights(self,save_path):
        ori_weight_dict=self.state_dict()
        quanted_weight_dict={}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key]=diff_quantized_tensor(ori_weight_dict[key],self.num_bits)
        torch.save(quanted_weight_dict,save_path)

class QuantDecoderSDF(nn.Module):
    def __init__(self,args):
        super().__init__()
        self.args=args
        decoder_dim_list=[int(decoder_dim) for decoder_dim in args.decoder_dim_list.split('_')]
        decoder_stride_list=[int(decoder_stride) for decoder_stride in args.decoder_stride_list.split('_')]

        embed_dim=args.embed_dim

        bias = args.bias             
        act=args.act                      
        conv_type=args.conv_type   

        after_embed_dim=args.after_embed_dim        

        num_bits=args.num_bits
        self.num_bits=args.num_bits

        decoder_layers_list=[]
        if after_embed_dim>0:
            decoder_layers_list.append(QuantConv3d(embed_dim,after_embed_dim,1,1,bias=bias,num_bits=num_bits))
        else:
            after_embed_dim=embed_dim

        for i in range(len(decoder_dim_list)):
            if i==0:
                in_channel=after_embed_dim
                out_channel=decoder_dim_list[i]
                scale=decoder_stride_list[i]
            else:
                in_channel=decoder_dim_list[i-1]
                out_channel=decoder_dim_list[i]
                scale=decoder_stride_list[i]
            decoder_layers_list.append(QuantNeRVBlock3D(in_channel,out_channel,scale,bias,act,conv_type,num_bits=num_bits))

        decoder_layers_list.append( QuantConv3d(decoder_dim_list[-1],1,3,1,1,bias=bias,num_bits=num_bits))
        self.decoder_layers=nn.Sequential(*decoder_layers_list)

    def forward(self,embed_features):
        #embed_features:    B,N,N,N,C
        embed_features=embed_features.permute(0,4,1,2,3)
        pred_voxel=self.decoder_layers(embed_features)  #(B,C,N,N,N)
        return pred_voxel.permute(0,2,3,4,1)        #(B,N,N,N,C)

    def get_quantparams(self):
        all_params=[]
        for param in self.parameters():
            all_params.append(diff_quantized_tensor(param.reshape(-1),self.num_bits))
        all_params=torch.cat(all_params,dim=0)
        return torch.mean(all_params) 
    
    def save_quanted_decoder_weights(self,save_path):
        ori_weight_dict=self.state_dict()
        quanted_weight_dict={}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key]=diff_quantized_tensor(ori_weight_dict[key],self.num_bits)
        torch.save(quanted_weight_dict,save_path)



class Downsample3D(nn.Module):
    """
    Reduce (B, D, Rx, Ry, Rz) -> (B, D, rx, ry, rz) using conv with kernel=stride=factor.
    factor must be integer and voxel_res must be divisible by token_res.
    """
    def __init__(self, in_ch, factor: Tuple[int,int,int]):
        super().__init__()
        fx, fy, fz = factor
        assert fx>=1 and fy>=1 and fz>=1
        self.factor = factor
        # Use depthwise conv preserving channels then a 1x1 conv to mix if needed
        # depthwise:
        self.depthwise = nn.Conv3d(in_ch, in_ch, kernel_size=(fx,fy,fz), stride=(fx,fy,fz), groups=in_ch, bias=False)
        self.pointwise = nn.Conv3d(in_ch, in_ch, kernel_size=1, stride=1)

    def forward(self, x):
        # x: (B, D, Rx, Ry, Rz)
        x = self.depthwise(x)
        x = self.pointwise(x)
        return x  # (B, D, rx, ry, rz)

class Upsample3D(nn.Module):
    """
    Upsample (B, D, rx, ry, rz) -> (B, D, Rx, Ry, Rz) using trilinear interpolation followed by 1x1 conv.
    """
    def __init__(self, in_ch, out_ch):
        super().__init__()
        self.proj = nn.Conv3d(in_ch, out_ch, kernel_size=1)

    def forward(self, x, target_size: Tuple[int,int,int]):
        # x: (B, D, rx, ry, rz)
        x = F.interpolate(x, size=target_size, mode="trilinear", align_corners=False)
        x = self.proj(x)
        return x  # (B, out_ch, Rx, Ry, Rz)



class InterpolationTransformerCrossAttnV6(nn.Module):
    """
    Decoupled-version:
    - voxel_feat_dim: per-voxel feature dimension (controls representational capacity)
    - transformer_dim: transformer's d_model (kept small)
    Preserves original forward signature.
    """
    def __init__(
        self,
        voxel_feat_dim=64,       # per-voxel feature size (can be large)
        transformer_dim=24,      # transformer's internal width (keeps transformer small)
        in_feat_dim=16,          # input channel count per voxel (original C_in)
        latent_dim=32,
        num_heads=4,
        num_enc_layers=2,
        num_dec_layers=2,
        group_size=5,
        voxel_res=(8, 8, 8),
        dropout=0.0,
        time_freqs=16,
        use_film=True,
        use_coord_embed=True,
        num_bits=32,             # quantization bits used by QuantLinear; set 32 to disable fake quant
    ):
        super().__init__()
        assert transformer_dim % num_heads == 0, "transformer_dim must be divisible by num_heads"
        self.voxel_feat_dim = voxel_feat_dim
        self.transformer_dim = transformer_dim
        self.in_feat_dim = in_feat_dim
        self.latent_dim = latent_dim
        self.group_size = group_size
        self.voxel_res = voxel_res
        self.num_voxels = voxel_res[0] * voxel_res[1] * voxel_res[2]
        self.use_film = use_film
        self.use_coord_embed = use_coord_embed
        self.num_bits = num_bits

        # Norms & small scales
        self.norm_prior = nn.LayerNorm(transformer_dim)
        self.norm_query = nn.LayerNorm(transformer_dim)
        self.norm_cond = nn.LayerNorm(transformer_dim)
        self.norm_decoded = nn.LayerNorm(transformer_dim)
        self.prior_scale = nn.Parameter(torch.tensor(0.5))
        self.residual_scale = nn.Parameter(torch.tensor(1.0))

        # -------------------------
        # Voxel encoder: maps input features -> voxel_feat_dim
        # -------------------------
        # If input features equal voxel_feat_dim, skip projector; else small MLP
        if in_feat_dim != voxel_feat_dim:
            self.input_voxel_encoder = nn.Sequential(
                QuantLinear(in_feat_dim, voxel_feat_dim, num_bits=num_bits),
                nn.GELU(),
                QuantLinear(voxel_feat_dim, voxel_feat_dim, num_bits=num_bits),
            )
        else:
            self.input_voxel_encoder = nn.Identity()

        # -------------------------
        # Project voxel_feat_dim -> transformer_dim BEFORE transformer
        # -------------------------
        self.voxel_to_trans = nn.Sequential(
            QuantLinear(voxel_feat_dim, transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(transformer_dim, transformer_dim, num_bits=num_bits),
        )

        # And the inverse: transformer_dim -> voxel_feat_dim after decoder
        self.trans_to_voxel = nn.Sequential(
            QuantLinear(transformer_dim, transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(transformer_dim, voxel_feat_dim, num_bits=num_bits),
        )

        # -------------------------
        # Coordinate embeddings (project to transformer_dim)
        # -------------------------
        rx, ry, rz = voxel_res
        xs = torch.linspace(-1.0, 1.0, rx)
        ys = torch.linspace(-1.0, 1.0, ry)
        zs = torch.linspace(-1.0, 1.0, rz)
        coords = torch.stack(torch.meshgrid(xs, ys, zs, indexing="ij"), dim=-1).reshape(-1, 3)
        self.register_buffer("coords", coords, persistent=False)
        if self.use_coord_embed:
            self.coord_proj = QuantLinear(3, transformer_dim, num_bits=num_bits)

        # -------------------------
        # Time embedding (maps to transformer_dim)
        # -------------------------
        self.time_embed = SinusoidalTimeEmbed(num_frequencies=time_freqs)
        self.time_to_token = QuantLinear(2 * time_freqs, transformer_dim, num_bits=num_bits)

        # Token type embedding (start/end)
        self.type_embed = nn.Embedding(2, transformer_dim)

        # -------------------------
        # Latent conditioning: map latent -> transformer_dim
        # -------------------------
        self.latent_cond_mlp = nn.Sequential(
            QuantLinear(latent_dim + 2 * time_freqs, transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(transformer_dim, transformer_dim, num_bits=num_bits)
        )

        if self.use_film:
            self.film_head = QuantLinear(transformer_dim, 2 * transformer_dim, num_bits=num_bits)

        # -------------------------
        # Prior MLP: operate in transformer_dim space
        # -------------------------
        self.prior_mlp = nn.Sequential(
            QuantLinear(3 * transformer_dim, 2 * transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(2 * transformer_dim, transformer_dim, num_bits=num_bits)
        )

        self.query_fuse = nn.Sequential(
            QuantLinear(2 * transformer_dim, 2 * transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(2 * transformer_dim, transformer_dim, num_bits=num_bits)
        )

        # -------------------------
        # Transformer (works at transformer_dim)
        # -------------------------
        enc_layer = nn.TransformerEncoderLayer(
            d_model=transformer_dim,
            nhead=num_heads,
            batch_first=True,
            dropout=dropout,
            dim_feedforward=transformer_dim * 4,
        )
        self.encoder = nn.TransformerEncoder(enc_layer, num_layers=num_enc_layers)

        dec_layer = nn.TransformerDecoderLayer(
            d_model=transformer_dim,
            nhead=num_heads,
            batch_first=True,
            dropout=dropout,
            dim_feedforward=transformer_dim * 4,
        )
        self.decoder = nn.TransformerDecoder(dec_layer, num_layers=num_dec_layers)

        # -------------------------
        # Output / residual head (in transformer space), then backproject to voxel_feat_dim
        # -------------------------
        self.output_layer = nn.Sequential(
            QuantLinear(transformer_dim, transformer_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(transformer_dim, transformer_dim, num_bits=num_bits),
        )

        self.dropout = nn.Dropout(dropout)

    # -------------------------
    # helper: expand coords and project to transformer_dim
    # -------------------------
    def _expand_coords(self, B, device):
        if not self.use_coord_embed:
            return None
        coords = self.coords.to(device).unsqueeze(0).expand(B, -1, -1)  # (B, V, 3)
        return self.coord_proj(coords)  # (B, V, transformer_dim)

    # -------------------------
    # helper: time token builder
    # -------------------------
    def _build_time_token(self, t: torch.Tensor) -> torch.Tensor:
        te = self.time_embed(t)              # (..., 2L)
        t_token = self.time_to_token(te)     # (..., transformer_dim)
        return t_token

    # -------------------------
    # Save / load quantized weights (lossless wrapper)
    # Note: keep these if you used them in previous code.
    # -------------------------
    def save_quanted_weights(self,save_path, num_bits):
        ori_weight_dict=self.state_dict()
        quanted_weight_dict={}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key]=diff_quantized_tensor(ori_weight_dict[key],num_bits)
        torch.save(quanted_weight_dict,save_path)
    def save_quanted_weights_lossless(self, save_path):
        ori_weight_dict = self.state_dict()
        quanted_weight_dict = {}
        for key, weight in ori_weight_dict.items():
            # Keep small/sensitive tensors in FP32
            if weight.numel() < 32 or "norm" in key.lower() or "bias" in key.lower() or "scale" in key.lower():
                quanted_weight_dict[key] = {"fp32": weight.cpu()}
                continue
            w_min, w_max = weight.min(), weight.max()
            scale = (w_max - w_min) / (2 ** 8 - 1)  # example using 8 bits for storage
            if scale == 0:
                quanted_weight_dict[key] = {"fp32": weight.cpu()}
                continue
            zero_point = torch.round(-w_min / scale)
            q_weight = torch.clamp(torch.round(weight / scale + zero_point), 0, 2 ** 8 - 1).to(torch.uint8)
            quanted_weight_dict[key] = {
                "q_weight": q_weight.cpu(),
                "scale": scale.cpu(),
                "zero_point": zero_point.cpu(),
            }
        torch.save(quanted_weight_dict, save_path)

    def load_quanted_weights_lossless(self, load_path, map_location="cpu"):
        quanted_weight_dict = torch.load(load_path, map_location=map_location)
        state_dict = {}
        for key, pack in quanted_weight_dict.items():
            if "fp32" in pack:
                state_dict[key] = pack["fp32"]
            else:
                q_weight = pack["q_weight"].float()
                scale = pack["scale"]
                zero_point = pack["zero_point"]
                state_dict[key] = (q_weight - zero_point) * scale
        self.load_state_dict(state_dict, strict=False)

    # -------------------------
    # The forward pass keeps same API:
    # f_start, f_end: (B, rx, ry, rz, C_in)
    # d_codes: (B, Gm, latent_dim)
    # returns: (B, Gm, rx, ry, rz, voxel_feat_dim)
    # -------------------------
    def forward(self, f_start, f_end, d_codes):
        """
        f_start, f_end: (B, rx, ry, rz, C_in)
        d_codes: (B, Gm, latent_dim) where Gm = group_size - 2
        returns pred: (B, Gm, rx, ry, rz, voxel_feat_dim)
        """
        B = f_start.shape[0]
        rx, ry, rz = self.voxel_res
        V = self.num_voxels
        device = f_start.device
        Gm = self.group_size - 2
        assert d_codes.shape[1] == Gm, "d_codes must provide one latent per intermediate step"

        # ---- flatten inputs ----
        f1 = f_start.reshape(B, V, -1)  # (B, V, C_in)
        fn = f_end.reshape(B, V, -1)

        # ---- encode to voxel feature space ----
        f1 = self.input_voxel_encoder(f1)  # (B, V, voxel_feat_dim)
        fn = self.input_voxel_encoder(fn)

        # ---- project voxel features to transformer space ----
        f1_t = self.voxel_to_trans(f1)  # (B, V, transformer_dim)
        fn_t = self.voxel_to_trans(fn)  # (B, V, transformer_dim)

        # ---- coord embeddings ----
        coord_tok = self._expand_coords(B, device)  # (B, V, transformer_dim) or None
        if coord_tok is not None:
            f1_t = f1_t + coord_tok
            fn_t = fn_t + coord_tok

        # ---- add time/type tokens (t=0, t=1) ----
        t0_tok = self._build_time_token(torch.zeros((), device=device)).expand(B, V, -1)  # (B, V, D_t)
        t1_tok = self._build_time_token(torch.ones((), device=device)).expand(B, V, -1)
        f1_t = f1_t + t0_tok + self.type_embed(torch.zeros(V, dtype=torch.long, device=device)).unsqueeze(0)
        fn_t = fn_t + t1_tok + self.type_embed(torch.ones(V, dtype=torch.long, device=device)).unsqueeze(0)

        # ---- encode memory from [start, end] ----
        memory_tokens = torch.cat([f1_t, fn_t], dim=1)  # (B, 2V, D_t)
        memory = self.encoder(memory_tokens)             # (B, 2V, D_t)

        # ---- build per-frame queries ----
        alphas = torch.arange(1, self.group_size - 1, device=device, dtype=torch.float32) / float(self.group_size - 1)
        t_tok = self._build_time_token(alphas)  # (Gm, D_t)

        # latent conditioning
        time_embed_for_latent = self.time_embed(alphas)                 # (Gm, 2L)
        cond_in = torch.cat([d_codes, time_embed_for_latent.unsqueeze(0).expand(B, -1, -1)], dim=-1)  # (B, Gm, latent+2L)
        cond = self.latent_cond_mlp(cond_in)                           # (B, Gm, D_t)
        cond = self.norm_cond(cond)
        if self.use_film:
            gamma_beta = self.film_head(cond)                           # (B, Gm, 2D_t)
            gamma, beta = gamma_beta.chunk(2, dim=-1)

        # repeat memory for each frame
        memory_rep = memory.unsqueeze(1).expand(B, Gm, -1, -1).reshape(B * Gm, -1, self.transformer_dim)  # (B*Gm, 2V, D_t)

        # prepare endpoint tensors repeated over frames (transformer dim)
        f1_rep = f1_t.unsqueeze(1).expand(B, Gm, V, self.transformer_dim).reshape(B * Gm, V, self.transformer_dim)
        fn_rep = fn_t.unsqueeze(1).expand(B, Gm, V, self.transformer_dim).reshape(B * Gm, V, self.transformer_dim)

        # time token per frame expanded to voxels
        t_tok_rep = t_tok.unsqueeze(0).unsqueeze(2).expand(B, -1, V, -1).reshape(B * Gm, V, self.transformer_dim)

        # ---- neural interpolator prior over [f1, fn, t] ----
        prior_in = torch.cat([f1_rep, fn_rep, t_tok_rep], dim=-1)  # (B*Gm, V, 3*D_t)
        prior = self.prior_mlp(prior_in)                           # (B*Gm, V, D_t)
        prior = self.norm_prior(prior)

        # ---- FiLM conditioning ----
        if self.use_film:
            gamma_rep = gamma.reshape(B * Gm, 1, self.transformer_dim).expand(-1, V, -1)
            beta_rep  = beta.reshape(B * Gm, 1, self.transformer_dim).expand(-1, V, -1)
            query = prior * (1.0 + gamma_rep) + beta_rep
        else:
            query = prior
        #query = self.norm_query(query)

        # concat & fuse conditioning
        cond_rep = cond.reshape(B * Gm, 1, self.transformer_dim).expand(-1, V, -1)
        query = self.query_fuse(torch.cat([query, cond_rep], dim=-1))
        query = self.dropout(query)

        # ---- decode with cross-attention (transformer) ----
        decoded = self.decoder(query, memory_rep)      # (B*Gm, V, D_t)
        decoded = self.norm_decoded(decoded)

        # predict residuals in transformer space
        residuals = self.output_layer(decoded)         # (B*Gm, V, D_t)
        pred_trans = self.prior_scale * prior + self.residual_scale * residuals  # (B*Gm, V, D_t)

        # ---- map transformer outputs back to voxel feature space ----
        pred_trans = pred_trans.reshape(B * Gm, V, self.transformer_dim)
        pred_voxel = self.trans_to_voxel(pred_trans)  # (B*Gm, V, voxel_feat_dim)

        # reshape back to (B, Gm, rx, ry, rz, voxel_feat_dim)
        pred_voxel = pred_voxel.reshape(B, Gm, rx, ry, rz, self.voxel_feat_dim)
        return pred_voxel





import torch
import torch.nn as nn
import numpy as np
import glob
import os
import re

# Assuming SinusoidalTimeEmbed is defined elsewhere, e.g.:
class SinusoidalTimeEmbed(nn.Module):
    def __init__(self, num_frequencies=16):
        super().__init__()
        self.num_frequencies = num_frequencies

    def forward(self, t: torch.Tensor) -> torch.Tensor:
        freqs = torch.pow(2, torch.arange(self.num_frequencies, device=t.device, dtype=torch.float32))
        angles = t.unsqueeze(-1) * freqs.unsqueeze(0)  # (..., 1) * (L,) -> (..., L)
        return torch.cat([torch.sin(angles), torch.cos(angles)], dim=-1)  # (..., 2L)

class PointNetEncoder(nn.Module):
    """ Enhanced PointNet-style encoder: (N,3) -> latent_dim with BatchNorm for stability.
    Updated to use Conv1d for proper channels-first processing on point clouds.
    """
    def __init__(self, hidden=128, out_dim=128):  # Increased defaults for richer features
        super().__init__()
        self.mlp = nn.Sequential(
            nn.Conv1d(3, hidden, 1),
            nn.BatchNorm1d(hidden),  # Stabilizes activations
            nn.ReLU(),
            nn.Conv1d(hidden, hidden, 1),
            nn.BatchNorm1d(hidden),
            nn.ReLU(),
            nn.Conv1d(hidden, out_dim, 1)
        )
        self.norm = nn.LayerNorm(out_dim)  # Normalize across feature dim

    def forward(self, pts: torch.Tensor) -> torch.Tensor:
        # pts: (..., N, 3)
        feat = self.mlp(pts.transpose(-1, -2))  # (B, 3, N) -> (B, out_dim, N)
        feat = feat.transpose(-1, -2)  # (B, N, out_dim)
        z = feat.max(dim=-2).values  # Symmetric max-pool over points (dim=-2 = N)
        return self.norm(z)

class LatentMapperPointNet(nn.Module):
    def __init__(self, latent_dim=32, point_feat_dim=128, time_freqs=16, hidden=256):  # Increased capacity
        super().__init__()
        self.pointnet = PointNetEncoder(hidden=128, out_dim=point_feat_dim)
        self.time_embed = SinusoidalTimeEmbed(num_frequencies=time_freqs)
        in_dim = (point_feat_dim * 4) + (2 * time_freqs)  # Simplified: z_s, z_e, z_t, dz_es + t_emb
        self.mlp = nn.Sequential(
            nn.Linear(in_dim, hidden),
            nn.GELU(),
            nn.Linear(hidden, hidden),
            nn.GELU(),
            nn.Linear(hidden, latent_dim)
        )
        self.out_norm = nn.LayerNorm(latent_dim)
        self.output_scale = nn.Parameter(torch.tensor(0.1))  # Learnable scale, start small for stability
        # No zero-init: Let default init provide variance

    def save_quanted_weights(self,save_path, num_bits):
        ori_weight_dict=self.state_dict()
        quanted_weight_dict={}
        for key in ori_weight_dict.keys():
            quanted_weight_dict[key]=diff_quantized_tensor(ori_weight_dict[key],num_bits)
        torch.save(quanted_weight_dict,save_path)

    def forward(self, pts_s, pts_e, pts_t, alpha):
        z_s = self.pointnet(pts_s)
        z_e = self.pointnet(pts_e)
        z_t = self.pointnet(pts_t)
        dz_es = 0.5 * (z_e - z_s)  # Overall delta, scaled for stability
        t_emb = self.time_embed(alpha)
        feat = torch.cat([z_s, z_e, z_t, dz_es, t_emb], dim=-1)
        z = self.mlp(feat)
        z = self.out_norm(z) * self.output_scale  # Scaled output
        if self.training:
            z += torch.randn_like(z) * 0.01  # Light noise for exploration
        return z

# Updated Loader with normalization
def load_frame_points(
    pattern: str,
    device: torch.device,
    sort_key=r"(\d+)$",  # extracts the trailing frame index
    assume_extension=None  # e.g., ".npy" or ".txt"; if None, infer
) -> torch.Tensor:
    """ pattern: glob pattern to match files, e.g. '/path/dancer_fr0res_2048_*'
    Returns: points (T, N, 3) tensor ordered by frame index. Each file must contain N points (N,3) inside the mesh surface.
    Now with per-frame normalization to [-1,1] for stability.
    """
    files = glob.glob(pattern)
    if not files:
        raise FileNotFoundError(f"No files matched pattern: {pattern}")
    # Sort by trailing integer in filename
    rx = re.compile(sort_key)
    def frame_idx(p):
        m = rx.search(os.path.splitext(p)[0])
        return int(m.group(1)) if m else 0
    files.sort(key=frame_idx)
    all_points = []
    for fp in files:
        ext = assume_extension or os.path.splitext(fp)[1].lower()
        if ext in (".npy",):
            P = np.load(fp)  # (N,3)
        elif ext in (".npz",):
            data = np.load(fp)
            P = data["arr_0"] if "arr_0" in data else data[list(data.keys())[0]]
        else:
            P = np.loadtxt(fp)  # Assume whitespace/commas
        assert P.ndim == 2 and P.shape[1] == 3, f"Bad shape in {fp}: {P.shape}"
        
        # ✅ Normalize: Center and scale to [-1,1]
        P_min, P_max = P.min(axis=0), P.max(axis=0)
        P = 2 * (P - P_min) / (P_max - P_min + 1e-6) - 1
        
        all_points.append(P)
    # (T, N, 3)
    points = torch.from_numpy(np.stack(all_points, axis=0)).float().to(device)
    return points

def build_latent_codes_from_points(
    indices: torch.Tensor,  # (B, group_size)
    points: torch.Tensor,  # (T, N, 3)
    mapper: LatentMapperPointNet,
    zero_based=True
):
    """ Returns: d_codes (B, Gm, latent_dim) """
    device = points.device
    B, G = indices.shape
    Gm = G - 2
    idx = indices - (0 if zero_based else 1)
    s_idx = idx[:, 0]  # (B,)
    e_idx = idx[:, -1]  # (B,)
    mid_idx = idx[:, 1:-1]  # (B, Gm)
    # Gather points
    pts_s = points[s_idx]  # (B, N, 3)
    pts_e = points[e_idx]  # (B, N, 3)
    pts_t = points[mid_idx]  # (B, Gm, N, 3)
    # Expand start/end across Gm
    pts_s = pts_s.unsqueeze(1).expand(-1, Gm, -1, -1)  # (B, Gm, N, 3)
    pts_e = pts_e.unsqueeze(1).expand(-1, Gm, -1, -1)  # (B, Gm, N, 3)
    # Normalized time positions α
    alphas = torch.arange(1, G - 1, device=device, dtype=torch.float32) / float(G - 1)
    alphas = alphas.unsqueeze(0).expand(B, -1)  # (B, Gm)
    # Flatten batch & frames for mapping
    Bm = B * Gm
    d_codes = mapper(
        pts_s.reshape(Bm, -1, 3),
        pts_e.reshape(Bm, -1, 3),
        pts_t.reshape(Bm, -1, 3),
        alphas.reshape(Bm)
    )
    return d_codes.reshape(B, Gm, -1)


def get_network(name,args):
    if name == "QuantDecoder":
        return QuantDecoder(args)
    elif name =='QuantDecoderSDF':
        return QuantDecoderSDF(args)
    elif name == "QuantGeneratorV2":
        return QuantGeneratorV2(args)
    else:
        assert False, 'no selected network !!!'

if __name__=='__main__':
    
    
    os.environ['CUDA_VISIBLE_DEVICES']='0'
    from config_load import get_config
    args=get_config().parse_args()
    
    net=QuantGeneratorV2(args).cuda()
    print(net)
    #net.save_quanted_decoder_weights('test.pt')

    #net.decoder.load_state_dict(torch.load('test.pt'))

    input=torch.rand(10,128,128,128, 4).cuda()

    output=net(input)

    print(output[1].size())
    print(net.num_bits)

    total_params = sum(p.numel() for p in net.parameters())
    trainable_params = sum(p.numel() for p in net.parameters() if p.requires_grad)

    print(f"Total parameters: {total_params:,}")
    print(f"Trainable parameters: {trainable_params:,}")
