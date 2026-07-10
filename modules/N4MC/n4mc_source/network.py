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
  
def diff_quantized_tensor(input,num_bits=8,min=-1,max=1):
    quant=STEQuantize.apply
    scale=(max - min) / (2**num_bits)
    input=torch.clamp(input,min,max)
    quanted_tensor=quant((input-min)/(scale))*scale+min
    #quanted_tensor=torch.clamp(quanted_tensor,min,max)
    return quanted_tensor





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
        self.embed_dim = int(args.embed_dim)
        self.embed_hwd = int(args.embed_hwd)

        # Encoder (keeps spatial structure)
        self.encoder = ConvNeXt3D(
            stage_blocks=1,
            strds=encoder_stride_list,
            dims=encoder_dim_list,
            in_chans=4,
            drop_path_rate=0
        )
        encoder_out_ch = encoder_dim_list[-1]

        use_quant_proj = getattr(args, "use_quant_proj", True)
        if use_quant_proj:
            self.proj_conv = QuantConv3d(encoder_out_ch, self.embed_dim, kernel_size=1, stride=1, padding=0, bias=bias, num_bits=num_bits)
        else:
            self.proj_conv = nn.Conv3d(encoder_out_ch, self.embed_dim, kernel_size=1, stride=1, padding=0, bias=bias)

        decoder_layers_list = []
        for i in range(len(decoder_dim_list)):
            in_channel = self.embed_dim if i == 0 else decoder_dim_list[i-1]
            out_channel = decoder_dim_list[i]
            scale = decoder_stride_list[i]
            decoder_layers_list.append(
                QuantNeRVBlock3D(in_channel, out_channel, scale, bias, act, conv_type, num_bits=num_bits)
            )
        decoder_layers_list.append(
            QuantConv3d(decoder_dim_list[-1], 4, 3, 1, 1, bias=bias, num_bits=num_bits)
        )
        self.decoder = nn.Sequential(*decoder_layers_list)

    def forward(self, input_voxel=None, embed_features=None, bypass_quant_proj=False):

        if input_voxel is not None:
            B = input_voxel.size(0)
            x = input_voxel.permute(0, 4, 1, 2, 3) 
            enc = self.encoder(x)                   
            enc_resized = F.interpolate(
                enc, size=(self.embed_hwd, self.embed_hwd, self.embed_hwd),
                mode='trilinear', align_corners=False
            )  

            proj = self.proj_conv(enc_resized) 

            return proj.permute(0, 2, 3, 4, 1) 


        embed = embed_features.permute(0, 4, 1, 2, 3)  
        pred = self.decoder(embed)                     
        return pred.permute(0, 2, 3, 4, 1)             

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



class InterpolationTransformer(nn.Module):
    def __init__(
        self,
        embed_dim=64,
        in_feat_dim=16,
        latent_dim=32,
        num_heads=4,
        transformer_dim=24,
        num_enc_layers=2,
        num_dec_layers=2,
        group_size=5,
        voxel_res=(4, 4, 4),
        dropout=0,
        time_freqs=16,
        use_film=True,
        use_coord_embed=True,
        num_bits=8,  
    ):
        super().__init__()
        self.embed_dim = embed_dim
        self.in_feat_dim = in_feat_dim
        self.latent_dim = latent_dim
        self.group_size = group_size
        self.voxel_res = voxel_res
        self.num_voxels = voxel_res[0] * voxel_res[1] * voxel_res[2]
        self.use_film = use_film
        self.use_coord_embed = use_coord_embed
        self.num_bits = num_bits

        self.norm_prior = nn.LayerNorm(embed_dim)
        self.norm_query = nn.LayerNorm(embed_dim)
        self.norm_cond = nn.LayerNorm(embed_dim)
        self.norm_decoded = nn.LayerNorm(embed_dim)
        self.prior_scale = nn.Parameter(torch.tensor(0.5))
        self.residual_scale = nn.Parameter(torch.tensor(1.0))

        if self.in_feat_dim != self.embed_dim:
            self.in_proj = QuantLinear(self.in_feat_dim, self.embed_dim, num_bits=num_bits)
        else:
            self.in_proj = nn.Identity()

        rx, ry, rz = voxel_res
        xs = torch.linspace(-1.0, 1.0, rx)
        ys = torch.linspace(-1.0, 1.0, ry)
        zs = torch.linspace(-1.0, 1.0, rz)
        coords = torch.stack(torch.meshgrid(xs, ys, zs, indexing="ij"), dim=-1).reshape(-1, 3)
        self.register_buffer("coords", coords, persistent=False)
        if self.use_coord_embed:
            self.coord_proj = QuantLinear(3, embed_dim, num_bits=num_bits)

        self.time_embed = SinusoidalTimeEmbed(num_frequencies=time_freqs)
        self.time_to_token = QuantLinear(2 * time_freqs, embed_dim, num_bits=num_bits)

        self.type_embed = nn.Embedding(2, embed_dim)

        self.latent_cond_mlp = nn.Sequential(
            QuantLinear(latent_dim + 2 * time_freqs, embed_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(embed_dim, embed_dim, num_bits=num_bits)
        )
        if self.use_film:
            self.film_head = QuantLinear(embed_dim, 2 * embed_dim, num_bits=num_bits)

        self.prior_mlp = nn.Sequential(
            QuantLinear(3 * embed_dim, 2 * embed_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(2 * embed_dim, embed_dim, num_bits=num_bits)
        )

        self.query_fuse = nn.Sequential(
            QuantLinear(2 * embed_dim, 2 * embed_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(2 * embed_dim, embed_dim, num_bits=num_bits)
        )

        enc_layer = nn.TransformerEncoderLayer(
            d_model=embed_dim, nhead=num_heads,
            batch_first=True, dropout=dropout,
            dim_feedforward=embed_dim * 4
        )
        self.encoder = nn.TransformerEncoder(enc_layer, num_layers=num_enc_layers)

        dec_layer = nn.TransformerDecoderLayer(
            d_model=embed_dim, nhead=num_heads,
            batch_first=True, dropout=dropout,
            dim_feedforward=embed_dim * 4
        )
        self.decoder = nn.TransformerDecoder(dec_layer, num_layers=num_dec_layers)

        self.output_layer = nn.Sequential(
            QuantLinear(embed_dim, embed_dim, num_bits=num_bits),
            nn.GELU(),
            QuantLinear(embed_dim, embed_dim, num_bits=num_bits)
        )

        self.dropout = nn.Dropout(dropout)

    def _expand_coords(self, B, device):
        if not self.use_coord_embed:
            return None
        coords = self.coords.to(device).unsqueeze(0).expand(B, -1, -1)
        return self.coord_proj(coords)  # (B, V, D)

    def _build_time_token(self, t: torch.Tensor) -> torch.Tensor:
        """
        t: (...,) in [0,1]
        returns: (..., D)
        """
        te = self.time_embed(t) 
        t_token = self.time_to_token(te)
        return t_token

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
            if weight.numel() < 32 or "norm" in key.lower() or "bias" in key.lower() or "scale" in key.lower():
                quanted_weight_dict[key] = {
                    "fp32": weight.cpu()
                }
                continue

            w_min, w_max = weight.min(), weight.max()
            scale = (w_max - w_min) / (2 ** self.num_bits - 1)
            if scale == 0:
                quanted_weight_dict[key] = {
                    "fp32": weight.cpu()
                }
                continue

            zero_point = torch.round(-w_min / scale)

            q_weight = torch.clamp(
                torch.round(weight / scale + zero_point),
                0, 2 ** self.num_bits - 1
            ).to(torch.uint8)

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
            if "fp32" in pack:  # directly stored FP32 tensor
                state_dict[key] = pack["fp32"]
            else:
                q_weight = pack["q_weight"].float()
                scale = pack["scale"]
                zero_point = pack["zero_point"]
                state_dict[key] = (q_weight - zero_point) * scale

        self.load_state_dict(state_dict, strict=False)

    def forward(self, f_start, f_end, d_codes):
        B = f_start.shape[0]
        rx, ry, rz = self.voxel_res
        V = self.num_voxels
        device = f_start.device
        Gm = self.group_size - 2
        assert d_codes.shape[1] == Gm, "d_codes must provide one latent per intermediate step"


        f1 = f_start.reshape(B, V, -1)  
        fn = f_end.reshape(B, V, -1)   
        f1 = self.in_proj(f1)          
        fn = self.in_proj(fn)          

        coord_tok = self._expand_coords(B, device)  
        if coord_tok is not None:
            f1 = f1 + coord_tok
            fn = fn + coord_tok

        t0_tok = self._build_time_token(torch.zeros((), device=device)).expand(B, V, -1) 
        t1_tok = self._build_time_token(torch.ones((), device=device)).expand(B, V, -1) 

        f1 = f1 + t0_tok + self.type_embed(torch.zeros(V, dtype=torch.long, device=device)).unsqueeze(0)
        fn = fn + t1_tok + self.type_embed(torch.ones(V, dtype=torch.long, device=device)).unsqueeze(0)


        memory_tokens = torch.cat([f1, fn], dim=1) 
        memory = self.encoder(memory_tokens)       

        alphas = torch.arange(1, self.group_size - 1, device=device, dtype=torch.float32) / float(self.group_size - 1)
        t_tok = self._build_time_token(alphas)  

        time_embed_for_latent = self.time_embed(alphas)          
        cond_in = torch.cat([d_codes, time_embed_for_latent.unsqueeze(0).expand(B, -1, -1)], dim=-1) 
        cond = self.latent_cond_mlp(cond_in)
        cond = self.norm_cond(cond)
        if self.use_film:
            gamma_beta = self.film_head(cond)                          
            gamma, beta = gamma_beta.chunk(2, dim=-1)               

        memory_rep = memory.unsqueeze(1).expand(B, Gm, -1, -1).reshape(B * Gm, -1, self.embed_dim)  

        f1_rep = f1.unsqueeze(1).expand(B, Gm, V, self.embed_dim).reshape(B * Gm, V, self.embed_dim) 
        fn_rep = fn.unsqueeze(1).expand(B, Gm, V, self.embed_dim).reshape(B * Gm, V, self.embed_dim)

        t_tok_rep = t_tok.unsqueeze(0).unsqueeze(2).expand(B, -1, V, -1).reshape(B * Gm, V, self.embed_dim) 

        prior_in = torch.cat([f1_rep, fn_rep, t_tok_rep], dim=-1) 
        prior = self.prior_mlp(prior_in)              
        prior = self.norm_prior(prior)

        if self.use_film:
            gamma_rep = gamma.reshape(B * Gm, 1, self.embed_dim).expand(-1, V, -1)
            beta_rep  = beta.reshape(B * Gm, 1, self.embed_dim).expand(-1, V, -1)
            query = prior * (1.0 + gamma_rep) + beta_rep       
        else:
            query = prior
        query = self.norm_query(query)
        cond_rep = cond.reshape(B * Gm, 1, self.embed_dim).expand(-1, V, -1)
        query = self.query_fuse(torch.cat([query, cond_rep], dim=-1))       
        query = self.dropout(query)

        decoded = self.decoder(query, memory_rep)   
        decoded = self.norm_decoded(decoded)

        residuals = self.output_layer(decoded)      
        pred = self.prior_scale * prior + self.residual_scale * residuals  

        pred = pred.reshape(B, Gm, rx, ry, rz, self.embed_dim)
        return pred


class SinusoidalTimeEmbed(nn.Module):
    def __init__(self, num_frequencies=16):
        super().__init__()
        self.num_frequencies = num_frequencies

    def forward(self, t: torch.Tensor) -> torch.Tensor:
        freqs = torch.pow(2, torch.arange(self.num_frequencies, device=t.device, dtype=torch.float32))
        angles = t.unsqueeze(-1) * freqs.unsqueeze(0) 
        return torch.cat([torch.sin(angles), torch.cos(angles)], dim=-1) 

class PointNetEncoder(nn.Module):

    def __init__(self, hidden=128, out_dim=128): 
        super().__init__()
        self.mlp = nn.Sequential(
            nn.Conv1d(3, hidden, 1),
            nn.BatchNorm1d(hidden),  
            nn.ReLU(),
            nn.Conv1d(hidden, hidden, 1),
            nn.BatchNorm1d(hidden),
            nn.ReLU(),
            nn.Conv1d(hidden, out_dim, 1)
        )
        self.norm = nn.LayerNorm(out_dim) 

    def forward(self, pts: torch.Tensor) -> torch.Tensor:
        feat = self.mlp(pts.transpose(-1, -2))  
        feat = feat.transpose(-1, -2)  
        z = feat.max(dim=-2).values  
        return self.norm(z)

class LatentMapperPointNet(nn.Module):
    def __init__(self, latent_dim=32, point_feat_dim=128, time_freqs=16, hidden=256): 
        super().__init__()
        self.pointnet = PointNetEncoder(hidden=128, out_dim=point_feat_dim)
        self.time_embed = SinusoidalTimeEmbed(num_frequencies=time_freqs)
        in_dim = (point_feat_dim * 4) + (2 * time_freqs)  
        self.mlp = nn.Sequential(
            nn.Linear(in_dim, hidden),
            nn.GELU(),
            nn.Linear(hidden, hidden),
            nn.GELU(),
            nn.Linear(hidden, latent_dim)
        )
        self.out_norm = nn.LayerNorm(latent_dim)
        self.output_scale = nn.Parameter(torch.tensor(0.1)) 

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
        dz_es = 0.5 * (z_e - z_s)  
        t_emb = self.time_embed(alpha)
        feat = torch.cat([z_s, z_e, z_t, dz_es, t_emb], dim=-1)
        z = self.mlp(feat)
        z = self.out_norm(z) * self.output_scale  
        if self.training:
            z += torch.randn_like(z) * 0.01  
        return z


def load_frame_points(
    pattern: str,
    device: torch.device,
    sort_key=r"(\d+)$",  
    assume_extension=None  
) -> torch.Tensor:
    
    files = glob.glob(pattern)
    if not files:
        raise FileNotFoundError(f"No files matched pattern: {pattern}")
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
            P = np.loadtxt(fp)  
        assert P.ndim == 2 and P.shape[1] == 3, f"Bad shape in {fp}: {P.shape}"
        
        P_min, P_max = P.min(axis=0), P.max(axis=0)
        P = 2 * (P - P_min) / (P_max - P_min + 1e-6) - 1
        
        all_points.append(P)
    points = torch.from_numpy(np.stack(all_points, axis=0)).float().to(device)
    return points

def build_latent_codes_from_points(
    indices: torch.Tensor, 
    points: torch.Tensor, 
    mapper: LatentMapperPointNet,
    zero_based=True
):
    device = points.device
    B, G = indices.shape
    Gm = G - 2
    idx = indices - (0 if zero_based else 1)
    s_idx = idx[:, 0]  
    e_idx = idx[:, -1]  
    mid_idx = idx[:, 1:-1]  

    pts_s = points[s_idx]  
    pts_e = points[e_idx]  
    pts_t = points[mid_idx]  

    pts_s = pts_s.unsqueeze(1).expand(-1, Gm, -1, -1)  
    pts_e = pts_e.unsqueeze(1).expand(-1, Gm, -1, -1)  

    alphas = torch.arange(1, G - 1, device=device, dtype=torch.float32) / float(G - 1)
    alphas = alphas.unsqueeze(0).expand(B, -1)  

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