# QNDF SSP INT8 experiment

This isolated variant trains the original QNDF architecture on an existing SSP mesh pair, then applies PyTorch dynamic INT8 quantization to every `Linear` layer.

It saves:

- FP32 and INT8 reconstructions in normalized and original coordinates
- the FP32 state dictionary
- loadable FP32 and INT8 TorchScript decoders for an equal-container size comparison
- `metrics.json` containing quality, artifact sizes, and reload verification

The original `Quantized-Neural-Displacement-Fields` folder is not modified. The INT8 TorchScript file is a real executable model artifact, but it is not the incomplete Huffman format commented out in the upstream script.

For the canonical historical bunny settings:

```bash
python compress_int8.py bunny -cs 3000 -ns 2 -hd 28 -nl 17 -ne 300
```
