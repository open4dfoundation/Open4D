import torch

orig_path = "/media/frozzzen/DataDrive/ChromeDownloads/Dancer_dataset/C4/TSDF/log/dancer_100/checkpoint_0400/decoder.pt"
compressed_path = "/path/to/decoder.ec"
restored_path = "/home/frozzzen/Documents/Github/Implicit-mesh-compression/decoder.pt"

# Original model state
orig_state = torch.load(orig_path, map_location="cpu")
# Decompressed model state
restored_state = torch.load(restored_path, map_location="cpu")

for k in orig_state:
    v1 = orig_state[k]
    v2 = restored_state[k]
    if torch.is_tensor(v1):
        print(f"{k}: orig={v1.shape} {v1.dtype}, restored={v2.shape} {v2.dtype}")
        print("equal:", torch.equal(v1, v2))
        diff = (v1 != v2).sum().item() if v1.dtype == v2.dtype else "dtype mismatch"
        print("mismatches:", diff)
    else:
        print(f"{k}: non-tensor entry, equal={v1==v2}")