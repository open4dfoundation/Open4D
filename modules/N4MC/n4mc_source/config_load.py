import configargparse 
import json

def get_config():
    parser = configargparse.ArgumentParser()
    parser.add_argument('--config_path',type=str, default=None, is_config_file=True, 
                            help='config file path')
    #--- network config
    parser.add_argument("--model", type=str,default='QuantGeneratorV2', 
                            help='')
    parser.add_argument("--encoder_dim_list", type=str,default='64_64_64_16', 
                            help='')
    parser.add_argument("--encoder_stride_list", type=str,default='4_2_2_2', 
                            help='')
    parser.add_argument("--decoder_dim_list", type=str,default='48_36_24_24', 
                            help='')
    parser.add_argument("--decoder_stride_list", type=str,default='4_2_2_2', 
                            help='')
    parser.add_argument("--after_embed_dim", type=int,default=64, 
                            help='')
    
    parser.add_argument("--bias", type=bool, default=True, 
                            help='')
    parser.add_argument("--act", type=str, default='gelu', 
                            help='')
    parser.add_argument("--conv_type", type=str, default='conv', 
                            help='')

    #--- train config
    parser.add_argument("--dataset", type=str, default='', 
                            help='')
    parser.add_argument("--data_path", type=str, default='', 
                            help='')
    parser.add_argument('--num_frames',type=int,default=0)
    parser.add_argument('--pin_memory',type=bool,default=True)
    parser.add_argument('--mask_threshold',type=float,default=1)

    parser.add_argument("--log_path", type=str, default='log_quantized_8bits', 
                            help='')
    parser.add_argument("--batch_size", type=int, default=1, 
                            help='')
    parser.add_argument("--n_epoch", type=int, default=300, 
                            help='')
    parser.add_argument("--val_frequence", type=int, default=20, 
                            help='')
    parser.add_argument("--voxel_grid_res", type=int, default=127, 
                            help='')
    
    parser.add_argument("--embed_dim", type=int, default=16, 
                            help='')
    parser.add_argument("--embed_hwd", type=int, default=4, 
                            help='')
    parser.add_argument('--init_method',type=str,default='normal',choices=['uniform','normal'])
    parser.add_argument('--embed_reg',type=float,default=0.1)
    

    parser.add_argument("--lr", type=float, default=1e-3, 
                            help='')
    parser.add_argument("--lr_type", type=str, default='cosine', 
                            help='')
    parser.add_argument("--lr_min", type=float, default=1e-5, 
                            help='')
    parser.add_argument("--device", type=str, default='cuda', 
                            help='')
    parser.add_argument("--warmup", type=float, default=0.2, 
                            help='')

    parser.add_argument('--important_weight',type=float,default=5)
    parser.add_argument('--ssim_weight',type=float,default=0.5)
    parser.add_argument('--offset_weight',type=float,default=10)

    parser.add_argument('--num_bits',type=int,default=8)

    parser.add_argument('--l1_reg',type=float,default=0)
    parser.add_argument("--autocodec_path", type=str, default=None)
    parser.add_argument("--group_size", type=int, default=5)
    parser.add_argument("--latent_dim", type=int, default=32)
    parser.add_argument("--surface_weight", type=float, default=2.0)
    parser.add_argument("--surface_band", type=float, default=0.02)


    #parser.add_argument('--lr_schedule',type=str,default=None)

    return parser

    #args = parser.parse_args()

def save_config(file_name, args):
    with open(file_name, 'w') as f:
        for arg, value in vars(args).items():
            f.write(f"{arg}: {value}\n")
if __name__=='__main__':
    #args=get_config().parse_args()
    parser=get_config()
    args=parser.parse_args()
    #save_config('config.txt',parser)
    print(args)
    save_config('config.txt',args)
    
    """if args.config_path:
        print('load config')
        args=load_config(args.config_path)
    print(args)
"""
    #args2=load_config('config.txt')
    #print(type(args2.channel_list))