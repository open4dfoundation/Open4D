import numpy as np
import os
import argparse
import struct

def convert_npy_to_bin(npy_path, bin_path):
    if os.path.exists(bin_path):
        print(f"⚠️  Warning: {bin_path} already exists. Skipping...")
        return

    # Load the .npy file
    data = np.load(npy_path)

    # Convert to float64
    data_f64 = data.astype(np.float64)

    # Write to binary file with header
    with open(bin_path, 'wb') as f:
        # Write dimensions as header (2 int32 values)
        rows, cols = data_f64.shape
        header = struct.pack('ii', rows, cols)
        f.write(header)

        # Write the actual data
        data_f64.tofile(f)

    print(f"✅ Converted: {npy_path} → {bin_path}")

def main():
    parser = argparse.ArgumentParser(
        description="Recursively convert delta_trajectories.npy to delta_trajectories.bin"
    )
    parser.add_argument(
        "--master_dir", type=str, required=True,
        help="Path to the master directory containing subdirectories"
    )
    args = parser.parse_args()

    for root, dirs, files in os.walk(args.master_dir):
        if "delta_trajectories.npy" in files:
            npy_path = os.path.join(root, "delta_trajectories.npy")
            bin_path = os.path.join(root, "delta_trajectories.bin")
            convert_npy_to_bin(npy_path, bin_path)

if __name__ == "__main__":
    main()
