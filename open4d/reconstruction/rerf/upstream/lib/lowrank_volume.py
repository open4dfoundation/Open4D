import torch
import torch.nn.functional as F


class TensorBase(torch.nn.Module):
    def __init__(self, gridSize, device, feat_dim = 48*4, cfg = None ):
        super(TensorBase, self).__init__()

        self.cfg = cfg

        self.feat_dim = feat_dim
        self.device=device

        self.update_stepSize(gridSize)
        self.init_svd_volume()

    def update_stepSize(self, gridSize):
      
        print("grid size", gridSize)
        self.gridSize= torch.LongTensor(gridSize).to(self.device)

    def init_svd_volume(self):
        pass

    def compute_features(self, xyz_sampled):
        pass
    

    def get_kwargs(self):
        return {
            'gridSize':self.gridSize.tolist(),
            'feat_dim': self.feat_dim,

        }

    def forward(self, ray_pts):
        feature = self.compute_features(ray_pts)
        return feature

class TensorDVGO(TensorBase):
    def __init__(self,  gridSize, device, **kargs):
        super(TensorDVGO, self).__init__(gridSize, device, **kargs)
        self.real_dim = self.feat_dim

    def init_svd_volume(self):
        self.k0 = torch.nn.Parameter(torch.empty([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)
        std = 1e-4
        self.k0.data.uniform_(-std, std)

    # xyz_sampled: (..., 3 )
    def compute_features(self, xyz_sampled):
        xyz_sampled = xyz_sampled.reshape(1,1,1,-1,3)
        return F.grid_sample(self.k0, xyz_sampled, mode='bilinear', align_corners=True).reshape(self.feat_dim,-1).T
    
    

    @torch.no_grad()
    def up_sampling_Vector(self, line, res_target):

        pass

    @torch.no_grad()
    def upsample_volume_grid(self, res_target):
        self.update_stepSize(res_target)
        self.k0 = torch.nn.Parameter(
            F.interpolate(self.k0.data, size=tuple(self.gridSize.tolist()), mode='trilinear', align_corners=True))

        
        print(f'upsamping to {res_target}')

class TensorDVGORes(TensorBase):
    '''
    Here k0 represents the difference between two frames of TensorDVGO k0
    '''
    def __init__(self,  gridSize, device, **kargs):
        super(TensorDVGORes, self).__init__(gridSize, device, **kargs)
        self.real_dim = self.feat_dim
        print("---------------init TensorDVGORes --------------")

    def init_svd_volume(self):
        self.k0 = torch.nn.Parameter(torch.empty([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)
        std = 1e-4
        self.k0.data.uniform_(-std, std)
        former_k0 = torch.tensor(torch.zeros([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)
        self.register_buffer('former_k0',former_k0)
        self.former_k0_cur = torch.tensor(torch.zeros([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)

    def compute_features(self, xyz_sampled):
        xyz_sampled = xyz_sampled.reshape(1, 1, 1, -1, 3)
        return F.grid_sample(self.former_k0_cur+self.k0, xyz_sampled, mode='bilinear', align_corners=True).reshape(self.feat_dim, -1).T

    @torch.no_grad()
    def up_sampling_Vector(self, line, res_target):
        pass


    @torch.no_grad()
    def upsample_volume_grid(self, res_target):
        self.update_stepSize(res_target)
        self.former_k0_cur= torch.tensor(
            F.interpolate(self.former_k0, size=tuple(self.gridSize.tolist()), mode='trilinear', align_corners=True))
        self.k0 = torch.nn.Parameter(
            F.interpolate(self.k0.data, size=tuple(self.gridSize.tolist()), mode='trilinear', align_corners=True))

        print(f'upsamping to {res_target}')

class TensorDVGODeform(TensorBase):
    '''
    former k0 is the full resolution k0 of the previous frame, k0=former_k0_cur
    '''
    def __init__(self,  gridSize, device, **kargs):
        super(TensorDVGODeform, self).__init__(gridSize, device, **kargs)
        self.real_dim = self.feat_dim
        print("---------------init TensorDVGORes --------------")

    def init_svd_volume(self):
        self.k0 = torch.nn.Parameter(torch.zeros([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)
        self.former_k0 = torch.tensor(torch.zeros([1, self.feat_dim, *self.gridSize.tolist()])).to(self.device)


    def compute_features(self, xyz_sampled):
        xyz_sampled = xyz_sampled.reshape(1, 1, 1, -1, 3)
        return F.grid_sample(self.k0, xyz_sampled, mode='bilinear', align_corners=True).reshape(self.feat_dim, -1).T

    @torch.no_grad()
    def up_sampling_Vector(self, line, res_target):
        pass

    @torch.no_grad()
    def upsample_volume_grid(self, res_target):
        self.update_stepSize(res_target)
        self.k0 = torch.nn.Parameter(
            F.interpolate(self.former_k0.data, size=tuple(self.gridSize.tolist()), mode='trilinear', align_corners=True))

        print(f'upsamping to {res_target}')


