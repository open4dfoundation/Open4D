import math
import os
import numpy as np
import torch
from torch.utils.data import Dataset
from glob import glob
from tqdm import tqdm


def _to_channel_first_numpy(volume: np.ndarray) -> np.ndarray:
    if volume.ndim == 4:
        volume = np.moveaxis(volume, -1, 0)
    elif volume.ndim == 3:
        volume = volume[None, ...]
    else:
        raise ValueError(f"Unexpected volume shape: {volume.shape}")
    return np.ascontiguousarray(volume.astype(np.float32, copy=False))


def _build_sdf_mask_tensors(sdf_grids: np.ndarray, mask_threshold: float):
    mask = (np.abs(sdf_grids) < mask_threshold).astype(np.float32)
    sdf_cf = _to_channel_first_numpy(sdf_grids)
    mask_cf = _to_channel_first_numpy(mask)
    return torch.from_numpy(sdf_cf), torch.from_numpy(mask_cf)


class SDF_dataset(Dataset):
    def __init__(self,args):
        super().__init__()
        data_path=args.data_path
        num_frames=args.num_frames
        pin_memory=args.pin_memory
        self.mask_threshold=args.mask_threshold
        self.data_list=[]
        self.sdf_data_path_list=[]
        self.pin_memory=pin_memory

        if not num_frames:
            num_frames=len(glob(os.path.join(data_path,'*','sdf_grid.npy')))

        for i in tqdm(range(num_frames)):
            
            self.sdf_data_path_list.append(os.path.join(data_path,'%04d'%i,'sdf_grid.npy'))

            if self.pin_memory: 
                sdf_grids=np.load(os.path.join(data_path,'%04d'%i,'sdf_grid.npy'))
                self.data_list.append(_build_sdf_mask_tensors(sdf_grids, self.mask_threshold))
                


    def __len__(self):

        return len(self.sdf_data_path_list)
    

    def __getitem__(self, index):
        if self.pin_memory:
            sdf,mask=self.data_list[index]
        else:
            sdf_grids=np.load(self.sdf_data_path_list[index])
            sdf, mask = _build_sdf_mask_tensors(sdf_grids, self.mask_threshold)

        return {'t':torch.tensor(float(index)/len(self)),
                'index': torch.tensor(index).long(),
                'sdf_offset':sdf,
                'mask':mask}
    
class SDF_dataset_npz(Dataset):
    def __init__(self,args):
        super().__init__()
        data_path=args.data_path
        num_frames=args.num_frames
        pin_memory=args.pin_memory
        self.mask_threshold=args.mask_threshold
        self.data_list=[]
        #self.sdf_data_path_list=[]
        #self.offset_data_path_list=[]
        self.npz_path_list=[]
        self.pin_memory=pin_memory

        if num_frames<1:
            num_frames=len(glob(os.path.join(data_path,'data','*.npz')))

        #print(os.path.join(data_path,'data','*.npz'))

        for i in tqdm(range(num_frames)):
            
            #self.sdf_data_path_list.append(os.path.join(data_path,'%04d'%i,'sdf_grid.npy'))
            #self.offset_data_path_list.append(os.path.join(data_path,'%04d'%i,'offset_grid.npy'))
            self.npz_path_list.append(os.path.join(data_path,'data','%04d.npz'%i))
            

            if self.pin_memory: 
                npz_data=np.load(os.path.join(data_path,'data','%04d.npz'%i))
                sdf_grids=npz_data['sdf']
                self.data_list.append(_build_sdf_mask_tensors(sdf_grids, self.mask_threshold))
                


    def __len__(self):

        return len(self.npz_path_list)
    

    def __getitem__(self, index):
        if self.pin_memory:
            sdf,mask=self.data_list[index]
        else:
            npz_data=np.load(self.npz_path_list[index])
            sdf_grids=npz_data['sdf']
            sdf, mask = _build_sdf_mask_tensors(sdf_grids, self.mask_threshold)

        return {'t':torch.tensor(float(index)/len(self)),
                'index':torch.tensor(index).long(),
                'sdf_offset':sdf,
                'mask':mask}


class SDF_dataset6(Dataset):
    def __init__(self,args):
        super().__init__()
        data_path=args.data_path
        num_frames=args.num_frames
        pin_memory=args.pin_memory
        self.mask_threshold=args.mask_threshold
        self.data_list=[]
        self.sdf_data_path_list=[]
        self.pin_memory=pin_memory

        if not num_frames:
            num_frames=len(glob(os.path.join(data_path,'*','sdf_grid.npy')))

        for i in tqdm(range(num_frames)):
            
            self.sdf_data_path_list.append(os.path.join(data_path,'%06d'%i,'sdf_grid.npy'))

            if self.pin_memory: 
                sdf_grids=np.load(os.path.join(data_path,'%06d'%i,'sdf_grid.npy'))
                self.data_list.append(_build_sdf_mask_tensors(sdf_grids, self.mask_threshold))

    def __len__(self):

        return len(self.sdf_data_path_list)
    
    def __getitem__(self, index):
        if self.pin_memory:
            sdf,mask=self.data_list[index]
        else:
            sdf_grids=np.load(self.sdf_data_path_list[index])
            sdf, mask = _build_sdf_mask_tensors(sdf_grids, self.mask_threshold)

        return {'t':torch.tensor(float(index)/len(self)),
                'index': torch.tensor(index).long(),
                'sdf_offset':sdf,
                'mask':mask}


class InterpolationDataset_old(Dataset):
    def __init__(self, dataset, args):
        self.dataset = dataset
        self.num_frames = args.num_frames
        self.group_size = args.group_size  # e.g., 5, 7, etc.
        self.device = args.device
        self.embed_feature_path = os.path.join(args.autocodec_path, "embed_features")

    def __len__(self):
        return self.num_frames - self.group_size + 1  # Number of sequences

    def __getitem__(self, idx):
        # Get sequence of group_size frames
        indices = list(range(idx, idx + self.group_size))
        #print("indices: ", indices, idx)
        # Load pre-computed embedded features
        embed_features = []
        for i in indices:
            file_path = os.path.join(self.embed_feature_path, f"embed_feature_{i:04d}.npy")
            embed_feature = np.load(file_path)
            embed_feature = torch.from_numpy(embed_feature).squeeze(0).float()  # Shape: (4, 4, 4, 16)
            #print("embed_feature: ", embed_feature.shape)
            embed_features.append(embed_feature)
        embed_features = torch.stack(embed_features)  # Shape: (group_size, 4, 4, 4, 16)

        return {
            "embed_features": embed_features,  # F_1, F_2, ..., F_group_size
            "indices": torch.tensor(indices, dtype=torch.long)
        }

class InterpolationDataset(Dataset):
    def __init__(self, dataset, args):
        self.dataset = dataset
        self.data_path = args.data_path
        self.mask_threshold = args.mask_threshold
        self.num_frames = args.num_frames
        self.group_size = args.group_size
        self.device = args.device
        self.embed_feature_path = os.path.join(args.autocodec_path, "embed_features")
        self.stride = args.group_size  # e.g., 1 for sliding window, args.group_size for no overlap

    def __len__(self):
        # With stride, you may get a leftover at the end → ceil ensures we keep it
        return math.ceil((self.num_frames - self.group_size) / self.stride) + 1

    def __getitem__(self, idx):
        start = idx * self.stride
        end = min(start + self.group_size, self.num_frames)
        indices = list(range(start, end))

        embed_features = []
        masks = []
        sdf_offsets = []

        for i in indices:
            # === Load embed features ===
            file_path = os.path.join(self.embed_feature_path, f"embed_feature_{i:04d}.npy")
            embed_feature = np.load(file_path)
            embed_feature = torch.from_numpy(embed_feature).squeeze(0).float()
            embed_features.append(embed_feature)

            # === Load sdf / mask ===
            npz_data = np.load(os.path.join(self.data_path, 'data', f"{i:04d}.npz"))
            sdf_grids = npz_data['sdf']  # shape: (..., 1)
            sdf, mask = _build_sdf_mask_tensors(sdf_grids, self.mask_threshold)
            masks.append(mask)
            sdf_offsets.append(sdf)


        #print("embded_features: ", len(embed_features))
        # === Padding if too short ===
        if len(embed_features) < self.group_size:
            feature_shape = embed_features[0].shape
            mask_shape = masks[0].shape
            sdf_shape = sdf_offsets[0].shape

            pad_count = self.group_size - len(embed_features)

            # Instead of zeros, repeat the last frame
            last_feat = embed_features[-1].unsqueeze(0).repeat(pad_count, *([1] * len(feature_shape)))
            last_mask = masks[-1].unsqueeze(0).repeat(pad_count, *([1] * len(mask_shape)))
            last_sdf = sdf_offsets[-1].unsqueeze(0).repeat(pad_count, *([1] * len(sdf_shape)))

            embed_features = torch.cat([torch.stack(embed_features), last_feat], dim=0)
            masks = torch.cat([torch.stack(masks), last_mask], dim=0)
            sdf_offsets = torch.cat([torch.stack(sdf_offsets), last_sdf], dim=0)

            indices.extend([indices[-1]] * pad_count)  # repeat last index
        else:
            embed_features = torch.stack(embed_features)
            masks = torch.stack(masks)
            sdf_offsets = torch.stack(sdf_offsets)

        return {
            "embed_features": embed_features,  # (group_size, D, H, W, C1)
            "masks": masks,  # (group_size, 1, D, H, W)
            "sdf_offsets": sdf_offsets,  # (group_size, 1, D, H, W)
            "indices": torch.tensor(indices, dtype=torch.long),
            "seq_id": idx
        }



def get_dataset(name,args):
    if name == 'SDF_dataset':
        return SDF_dataset(args)
    elif name == 'SDF_dataset6':
        return SDF_dataset6(args)
    elif name=='SDF_dataset_npz':
        return SDF_dataset_npz(args)
    else:
        assert False
