# Bunny INT8 run

Run completed on 2026-07-15 with the original SSP pair and these settings:

```text
cs=3000, ns=2, hd=28, nl=17, epochs=300, seed=20260715
```

| Measurement | FP32 | Dynamic INT8 | Change |
|---|---:|---:|---:|
| Total QNDF geometry error | 0.0011811415 | 0.0011962005 | +1.275% |
| Normal error | 3.315636° | 3.339728° | +0.727% |
| Loadable TorchScript decoder | 211,062 B | 213,330 B | +1.074% |

Both TorchScript decoders were saved, reloaded, and reproduced their pre-save predictions exactly (`max_abs_difference = 0`). Dynamic INT8 therefore caused a small quality loss but did not reduce the loadable artifact size for this small network; quantization metadata and container overhead outweighed the packed-weight savings.

The upstream commented Huffman experiment was not used because it does not provide a complete loadable decoder. Full machine-readable results are in `outputs/bunny_ssp_cs3000_ns2_300epochs_int8/metrics.json`.
