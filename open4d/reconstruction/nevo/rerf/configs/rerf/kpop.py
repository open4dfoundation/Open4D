_base_ = '../default.py'

expname = 'kpop'
basedir = './output'
half_res = True

train_mode = 'individual'
fix_rgbnet = True
deform_res_mode="separate"
res_lambda=1e-2
deform_lambda=1e-2
frame_num=4000
deform_low_reso=True
density_deform=False
data = dict(
    datadir='./data/kpop',
    dataset_type='NHR',
    inverse_y=True,
    white_bkgd=True,
)

deform_from_start=True

fine_train=dict(
N_iters=16000,
N_iters_pretrained=16000,
tv_every=1,# count total variation loss every tv_every step
tv_after=1000,# count total variation loss from tv_from step
tv_before=12000,         # count total variation before the given number of iterations
tv_dense_before=12000,      # count total variation densely before the given number of iterations
weight_tv_deform=1.,
weight_tv_density=0.000016,    # weight of total variation loss of density voxel grid
weight_tv_k0=0.0,       # weight of total variation loss of color/feature voxel grid
lrate_deformation_field=1e-4,
pg_scale=[ 2000, 4000, 6000],
)

fine_model_and_render=dict(
  num_voxels=250**3,
  num_voxels_base=250**3,
  rgbnet_dim=12,
)