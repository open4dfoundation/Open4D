import constriction
import numpy as np


D = np.load("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories.npy")
#D = np.loadtxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/B_matrix.txt")
print(f"Matrix shape: {D.shape}")
print(f"Matrix sample (first 5 rows):\n{D[:5, :]}")

# Quantize float values to integers, QuantizedGaussian model and AnsCoder can only be applied on integers.
scaling_factor = 10000
D_quantized = np.round(D * scaling_factor).astype(np.int32)
min_val, max_val = np.min(D_quantized), np.max(D_quantized)
print(f"Quantized data range: [{min_val}, {max_val}]")

# Define the QuantizedGaussian model
model_range = (min_val, max_val)
model_family = constriction.stream.model.QuantizedGaussian(*model_range)

# Flatten the matrix to a 1D array for encoding
symbols = D_quantized.flatten().astype(np.int32)  

# Estimate entropy model parameters (mean and std for each column)
means = np.zeros(len(symbols), dtype=np.float64)
stds = np.zeros(len(symbols), dtype=np.float64)
cols = D_quantized.shape[1]
for j in range(cols):
    col_data = D_quantized[:, j]
    mean = np.mean(col_data)
    std = np.std(col_data) if np.std(col_data) > 0 else 1.0  # Avoid zero std
    means[j::cols] = mean
    stds[j::cols] = std

# Encode the symbols
encoder = constriction.stream.stack.AnsCoder()
encoder.encode_reverse(symbols, model_family, means, stds)

# Get the compressed representation
compressed = encoder.get_compressed()
np.save("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories_encoded.npy", compressed)
print(f"Compressed representation: {compressed}")
print(f"Compressed size: {encoder.num_bits()} bits")
original_bits = D.nbytes * 8  # Assuming 64-bit floats
print(f"Compression ratio: {original_bits / encoder.num_bits():.2f}")
print(f"{encoder.num_bits():.2f}")

# Decode the symbols
decoder = constriction.stream.stack.AnsCoder(compressed)
reconstructed_quantized = decoder.decode(model_family, means, stds)
shape = D.shape
reconstructed = reconstructed_quantized.reshape(shape) / scaling_factor

# Verify reconstruction (with tolerance due to quantization)
mse = np.mean((D - reconstructed) ** 2)
print(f"Mean squared error: {mse:.2e}")
print(f"Reconstructed matrix sample (first 5 rows):\n{reconstructed[:5, :]}")
print("Matrix successfully encoded and decoded!")

np.save("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories_decoded.npy", D)
np.savetxt("/media/frozzzen/DataDrive/VS2022Projects/tvm-editing/TVMEditor.Test/bin/Release/net5.0/output/Answering/reference/delta_trajectories_decoded.txt", D)