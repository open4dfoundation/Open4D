import torch
from torch.utils.data import Dataset
import torch.nn as nn
import torch.nn.functional as F
from tqdm import tqdm

class PE(nn.Module):
    def __init__(self, pe_dim):
        super(PE, self).__init__()
        self.pe_dim = pe_dim
    def forward(self, x):
        st = []
        for j in range(self.pe_dim//2):
            st.append(torch.sin((2**j)*x*torch.pi))
            st.append(torch.cos((2**j)*x*torch.pi))
        return torch.cat(st, dim=1)

class MLP(nn.Module):
    def __init__(self, input_dim, hidden_dim, output_dim, num_layers):
        super(MLP, self).__init__()
        self.num_layers = num_layers
        # self.pe = PE(pe_dim)
        self.layers = nn.ModuleList([nn.Sequential(
            nn.Linear(input_dim if i == 0 else hidden_dim, hidden_dim),
            nn.SiLU(),
            nn.LayerNorm(hidden_dim),
        ) for i in range(num_layers)])
        self.neighbor_layers = nn.ModuleList([nn.Sequential(
            nn.Linear(input_dim if i == 0 else hidden_dim, hidden_dim),
            nn.SiLU(),
            nn.LayerNorm(hidden_dim),
        ) for i in range(3)])
        self.output_layer = nn.Linear(hidden_dim, output_dim)

    def forward(self, x, xn, ln):
        # x = self.pe(x)
        out = x
        for i in range(self.num_layers):
            # print(i, out.shape, self.layers[i].parameters)
            residual = out
            out = self.layers[i](out)
            if i<3: 
                xn = self.neighbor_layers[i](xn)
                x_acc = (xn*ln[:,:,None]).sum(dim=1)
                out = (out+x_acc)*0.5
            if i>0: out += residual
        out = self.output_layer(out)
        return out

class MeshDataset(Dataset):
    def __init__(self, pe_verts, verts, faces, gt_verts):
        self.pv = pe_verts
        self.v = verts
        self.f = faces
        self.gv = gt_verts
        self.preproc()
    def preproc(self):
        ln = self.pv.size(0)
        self.edge_wts = []
        self.neighbors = []
        for idx in tqdm(range(ln)):
            fnum, _ = torch.nonzero(self.f==idx, as_tuple=True)
            n_verts = torch.unique(self.f[fnum,:])
            n_verts = n_verts[n_verts!=idx]
            npv = self.pv[n_verts]
            edges = self.v[n_verts] - self.v[idx]
            edge_weights = F.softmax((edges**2).sum(dim=1)**0.5, dim=0)
            npv_padded = torch.zeros([14, npv.shape[1]], device=self.pv.device)
            ew_padded = torch.zeros(14, device=self.pv.device)
            npv_padded[:npv.shape[0],:] = npv[:14,:]
            ew_padded[:edge_weights.shape[0]] = edge_weights[:14]
            self.neighbors.append(npv_padded)
            self.edge_wts.append(ew_padded)
        self.neighbors = torch.stack(self.neighbors, dim=0)
        self.edge_wts = torch.stack(self.edge_wts, dim=0)
    def __len__(self):
        return self.pv.shape[0]
    def __getitem__(self, idx):
        return [self.pv[idx], self.neighbors[idx], self.edge_wts[idx], self.gv[idx]]

def parse_args():
    pr = ap.ArgumentParser()
    pr.add_argument("mesh_name", type=str, help="name of the mesh file to be compressed")
    pr.add_argument("--num_subdiv", "-ns", type=int, default=2, help="Number of subdivisions to perform on coarse mesh. Must be <=3")
    pr.add_argument("--coarse_size", "-cs", type=int,  default=4000, help="Number of faces in coarse representation")

    pr.add_argument("--pe_dim", "-pe", type=int, default=20, help="Number of positional enbedding features to be extracted")
    pr.add_argument("--hidden_dim", "-hd", type=int, default=56, help="Number of dimensions in the hidden layers")
    pr.add_argument("--num_layers", "-nl", type=int, default=20, help="Number of hidden layers")
    pr.add_argument("--input_scale", "-is", type=float, default=1000, help="Scale factor of the inputs")
    pr.add_argument("--output_scale", "-os", type=float, default=1414, help="Scale factor of the outputs")
    pr.add_argument("--run_suffix", "-rs", type=str, default="", help="a suffix to add to the run name")
    
    pr.add_argument("--learning_rate", "-lr", type=float, default=0.001, help="Learning rate")
    pr.add_argument("--num_epochs", "-ne", type=int, default=4000, help="Number of epochs")
    pr.add_argument("--batch_size", "-bs", type=int, default=2048, help="Batch size")
    pr.add_argument("--prune_factor", "-pf", type=float, default=0.12, help="Factor to prune the wights by")
    pr.add_argument("--prune_steps", "-ps", type=int, nargs='*', default=[40000000], help="Epochs at which to perform pruning")
    return pr.parse_args()

if __name__=="__main__":
    import argparse as ap
    from pytorch3d.io import load_obj, save_obj
    import torch.optim as optim
    from torch.utils.data import DataLoader
    from mesh_errors import point2mesh_error, normal_error
    import mlflow
    from torch.nn.utils import prune
    from torch.quantization import quantize_dynamic
    from dahuffman import HuffmanCodec
    import numpy as np
    import os

    args = parse_args() 

    # Load meshes
    lr_path = f'experiments/{args.mesh_name}/input_f{args.coarse_size}_s{args.num_subdiv}.obj'
    if not os.path.exists(lr_path):
        from build_dataset import run_binary
        run_binary(args.mesh_name, args.coarse_size, args.num_subdiv)
    lr,lf,_ = load_obj(lr_path, load_textures=0, device="cuda:0")
    lf = lf.verts_idx
    gt_path = f'experiments/{args.mesh_name}/output_f{args.coarse_size}_s{args.num_subdiv}.obj'
    gt,_,_ = load_obj(gt_path, load_textures=0, device="cuda:0")
    og_path = f'objs_original/{args.mesh_name}.obj'
    ov,of,_ = load_obj(og_path, load_textures=0, device="cuda:0")
    of = of.verts_idx
    mn,_ = ov.min(dim=0)
    ov -= mn
    ov /= ov.max()

    # Create dataset and dataloader
    ip = lr*args.input_scale
    mean = torch.mean(ip, dim=0, keepdim=True)
    std = torch.std(ip, dim=0, keepdim=True)
    ip -= mean
    ip /= std
    pe_inputs = PE(args.pe_dim)(ip)
    dset = MeshDataset(pe_inputs, ip, lf, (gt-lr)*args.output_scale)
    loader = DataLoader(dset, batch_size=2048, shuffle=False)
    
    # Initialize model and optimizer
    input_dim = 3
    output_dim = 3
    model = MLP(input_dim*args.pe_dim, args.hidden_dim, output_dim, args.num_layers).to(device="cuda:0")
    total_params = sum([p.data.nelement() for p in model.parameters()])
    params_to_prune = []
    for l in range(len(model.layers)):
        params_to_prune.append((model.layers[l][0], 'weight'))
    for l in range(len(model.neighbor_layers)):
        params_to_prune.append((model.neighbor_layers[l][0], 'weight'))
    optimizer = optim.AdamW(model.parameters(), lr=args.learning_rate)
    criterion = nn.MSELoss()
    scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, args.num_epochs)

    
    mlflow.set_tracking_uri("http://127.0.0.1:8080")
    mlflow.set_experiment(args.mesh_name)
    run_name = f'ns{args.num_subdiv}_cs{args.coarse_size}_hd{args.hidden_dim}_nl{args.num_layers}_{args.run_suffix}'
    artifact_path = run_name
    with mlflow.start_run(run_name=run_name):
        mlflow.log_params(vars(args))
        b = 500000000
        suff = 0
        for epoch in tqdm(range(args.num_epochs)):
            if epoch in args.prune_steps:
                prune.global_unstructured(params_to_prune,pruning_method=prune.L1Unstructured, amount=args.prune_factor)
                sparisity_num = 0.
                for param,_ in params_to_prune:
                    sparisity_num += (param.weight == 0).sum()
                mlflow.log_metric("sparsity", f"{sparisity_num / total_params}", step=epoch)
                b=500000000
                suff += 1

            model.train()
            running_loss = 0.0
            for inputs, neighbors, edge_weights, targets in loader:
                optimizer.zero_grad()
                outputs = model(inputs, neighbors, edge_weights)
                loss = criterion(outputs, targets)
                loss.backward()
                optimizer.step()
                running_loss += loss.item() * inputs.size(0)

            epoch_loss = running_loss / len(loader.dataset)
            mlflow.log_metric("train_loss", f"{epoch_loss}", step=epoch)
            scheduler.step()
            
            if (epoch%25)==0:
                model.eval()
                bs=40000
                tv = []

                with torch.no_grad():
                    tv.append(lr + model(pe_inputs, dset.neighbors, dset.edge_wts)/args.output_scale)
                tv = torch.cat(tv, dim=0)
                e,_,_ = point2mesh_error(tv,lf,ov,of)
                if e<b:
                    b=e
                    mlflow.log_metric("best_eval_loss", f"{e}", step=suff)
                    if suff==len(args.prune_steps):
                        torch.save(model.state_dict(), f'best_model.pth')
                        try:
                            mlflow.log_artifact(f'best_model.pth', artifact_path=artifact_path)
                        except:
                            pass
                        save_obj(f'prequant_reconstruction.obj', tv, lf)
                        try:
                            mlflow.log_artifact(f'prequant_reconstruction.obj', artifact_path=artifact_path)
                        except:
                            pass
                mlflow.log_metric("eval_loss", f"{e}", step=epoch)
                del tv
                torch.cuda.empty_cache()
        
        # Apply post training compression
        # model = model.to(device="cpu")
        # for param in params_to_prune:
        #     prune.remove(param[0], param[1])
        # mq = quantize_dynamic(model, qconfig_spec={nn.Linear}, dtype=torch.qint8, inplace=False).cpu()
        # tv = lr.cpu() + mq(pe_inputs.cpu(), dset.neighbors.cpu(), dset.edge_wts.cpu())/args.output_scale
        # tv = tv.cuda()
        tv = lr + model(pe_inputs, dset.neighbors, dset.edge_wts)/args.output_scale
        r2t,_,_ = point2mesh_error(tv,lf, ov, of)
        print(f'Rec to Tar Compression Error obtained: {r2t}')
        mlflow.log_metric("Rec2Tar Error", f"{r2t}")
        t2r,_,_ = point2mesh_error(ov, of, tv,lf)
        print(f'Tar to Rec Compression Error obtained: {t2r}')
        mlflow.log_metric("Tar2Rec Error", f"{t2r}")
        print(f'Total Compression Error obtained: {r2t+t2r}')
        mlflow.log_metric("Total Error", f"{r2t+t2r}")
        ne = normal_error(tv, lf, ov, of)
        print(f'Normal Error obtained: {ne}')
        mlflow.log_metric("Normal Error", f"{ne}")
        save_obj(f'reconstruction.obj', tv, lf)
        try:
            mlflow.log_artifact(f'reconstruction.obj', artifact_path=artifact_path)
        except:
            pass

        # Apply Entropy Coding
        # wt_list = []
        # size = 0
        # for param in mq.state_dict().items():
        #     name, param = param
        #     if type(param) is torch.dtype: continue
        #     if type(param) is tuple: 
        #         param = param[0]
        #         wt_list.append(param.flatten())
        #     size += param.nelement() * param.element_size()
        # cat_param = torch.cat(wt_list)
        # input_code_list = torch.int_repr(cat_param).tolist()
        # unique, counts = np.unique(input_code_list, return_counts=True)
        # num_freq = dict(zip(unique, counts))
        # codec = HuffmanCodec.from_data(input_code_list)
        # sym_bit_dict = {}
        # for k, v in codec.get_code_table().items():
        #     sym_bit_dict[k] = v[0]
        # total_bits = 0
        # for num, freq in num_freq.items():
        #     total_bits += freq * sym_bit_dict[num]
        # coded = codec.encode(input_code_list)
        # with open("coded_weights.bin", 'wb') as f: f.write(coded)
        # try:
        #     mlflow.log_artifact(f'coded_weights.bin', artifact_path=artifact_path)
        # except:
        #     pass
        # print(f'Size of Compressed MLP is {len(coded)} Bytes')
        # mlflow.log_metric("Coded MLP Size", f"{len(coded)}")
        # from math import ceil,log2
        # ofs = args.coarse_size
        # ovs = ofs//2
        # coarse_mem = ceil((ovs*3*32 + ofs*3*ceil(log2(ovs)))/8)
        # print(f'Size of Coarse Mesh is {coarse_mem} Bytes')
        # mlflow.log_metric("Coarse Mesh Size", f"{coarse_mem}")
        # print(f'Total Size of Compressed Representation is {len(coded)+coarse_mem} Bytes')
        # mlflow.log_metric("Compressed Representation Size", f"{len(coded)+coarse_mem}")

        # Print size of coarse mesh
        num_faces = args.coarse_size
        num_verts = num_faces//2
        from math import ceil,log2
        coarse_mem = ceil((num_verts*3*32 + num_faces*3*ceil(log2(num_verts)))/8)
        mlflow.log_metric("Coarse Mesh Size", f"{coarse_mem}")

        # Print size of uncompressed MLP
        wt_list = []
        size = 0
        for param in model.state_dict().items():
            name, param = param
            if type(param) is torch.dtype: continue
            if type(param) is tuple: 
                param = param[0]
                wt_list.append(param.flatten())
            size += param.nelement() * param.element_size()
        mlflow.log_metric("Coded MLP Size", f"{size}")
        mlflow.log_metric("Compressed Representation Size", f"{size+coarse_mem}")
        print(f'Total Size of Compressed Representation is {((size+coarse_mem)/1024):.2f}KB')

        try:
            os.remove('best_model.pth')
            os.remove('prequant_reconstruction.obj')
            os.remove('reconstruction.obj')
            os.remove('coded_weights.bin')
        except:
            pass
