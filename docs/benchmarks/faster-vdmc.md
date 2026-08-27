# Faster V-DMC benchmark report

This report records the August 2026 performance study behind
`open4d/codecs/faster_vdmc`. The optimized codec is a complete-source Git
submodule, not a partial wrapper: it forks MPEG V-DMC Test Model v14.0 at
`ecffe4212e5e956761c4fa14a17c453ae916b0b1` and is pinned by Open4D at
`93cdd5e1367b0f9f81c251ef89255bcb2f0d2d3f`. The original
`open4d/codecs/vdmc` submodule remains unchanged as the reference.

The machine-readable record is
[`faster-vdmc-2026-08-18.json`](faster-vdmc-2026-08-18.json). Raw meshes,
textures, decoded outputs, and logs are intentionally not committed; see the
[artifact policy](../artifacts.md).

## Result summary

| Experiment | Before | After | Result |
| --- | ---: | ---: | ---: |
| Exact encoder, three 10-frame windows | 128.653 s mean | 120.477 s mean | 6.35% less wall time |
| Texture transfer within that encoder | 12.658 s mean | 4.757 s mean | 2.66x faster |
| Start window, threaded HM to x265 | 119.71 s | 55.61 s | 2.15x faster |
| Two independent three-frame GOFs | 33.75 s serial | 17.12 s parallel | 1.97x throughput |
| Matched full-output decode, five-run mean | 1.844 s | 0.724 s | 2.55x faster |

The first and fourth results are byte-exact. The decoder optimization changes
only scheduling and reproduced all output files byte-for-byte. The x265 result
is not byte-exact and is a provisional throughput mode: its 119,157-byte start
window was 6.37% larger than threaded HM's 112,021-byte stream. Geometry output
matched HM for all ten frames, but decoded texture did not. The aggregate RGB
PSNR between the two decoded 1024x1024 texture atlases was 22.86 dB. This is a
delta against the HM output, not a source-quality metric.

## What was evaluated

The dataset was the 157-frame internal `Rafa_Approves_hd_4k` textured-mesh
sequence. Each inspected frame has approximately 20,000 vertices, exactly
40,000 triangle faces, and a 4096x4096 8-bit JPEG texture. Tests used the start,
middle, and end windows:

- frames 1-10;
- frames 74-83;
- frames 148-157.

The encoder used 12-bit input positions, 13-bit texture coordinates, two
subdivision iterations, 11-bit base-mesh positions, 10-bit base-mesh texture
coordinates, a 1024x1024 texture atlas, texture QP 44, and a ten-frame GOF.
Input-manifest and configuration hashes are in the JSON record.

The sequence is not redistributed by Open4D, and its redistribution/license
record has not been established in this repository. These numbers therefore
describe an internal engineering dataset, not a public benchmark corpus.

## Machine and build

Measurements ran on `frozzzen-Lambda-Vector`:

- Ubuntu 24.04, x86-64;
- AMD Ryzen Threadripper 7960X, 24 cores and 48 hardware threads;
- 131,380,832 KiB reported memory (about 125.3 GiB);
- GCC 13.3 Release builds;
- two NVIDIA GeForce RTX 4090 GPUs, 24,564 MiB each.

The tested V-DMC paths were CPU-only. The GPUs were present but did not produce
these speedups. Encoder runs were not CPU-pinned, and CPU frequency was not
fixed. The three-window encoder result is one measured run per window, so it is
useful engineering evidence rather than a confidence-bounded publication
benchmark. Decoder before/after results are five-run means.

## Where the reference encoder spends time

The mean stage split for the three reference ten-frame windows was:

| Stage | Seconds | Share |
| --- | ---: | ---: |
| Attribute/texture compression | 80.834 | 62.99% |
| Preprocessing | 31.644 | 24.66% |
| Base-mesh compression | 14.949 | 11.65% |
| Geometry-video compression | 0.894 | 0.70% |
| Container writer | 0.004 | less than 0.01% |

Texture/attribute compression was slow because the HM reference encoder
serially performs recursive coding-unit partition, motion, transform, and rate
distortion searches. Texture transfer accounted for 12.658 seconds inside that
stage. Preprocessing repeatedly performs mesh fitting, UVAtlas work, and
decimation; fitting was about 14.189 seconds and UVAtlas about 8.016 seconds.
Base-mesh compression repeats geometry parametrization/deformation work.

Geometry-video coding and bitstream serialization were already fast. After the
x265 experiment reduces attribute compression to 7.986 seconds, preprocessing
becomes the dominant measured stage, followed by base-mesh work.

## Implemented optimizations

The fork contains eight commits after MPEG v14.0:

1. Skip normal-inversion scans when the configured threshold makes a hit
   mathematically impossible.
2. Index HM input frames instead of repeatedly erasing and deep-copying frame
   zero.
3. Remove unused mapped-UV transfer structures and decimal-string keys.
4. Remove unread base-mesh and motion copies.
5. Preserve threshold-boundary behavior after the fast-path changes.
6. Parallelize texture transfer by output row while retaining triangle order,
   preserving the original last-triangle-wins rule.
7. Add opt-in x265 Main10/WPP encoding and independent-GOF process scheduling.
8. Parallelize independent texture conversion and output frames with bounded
   worker pools capped at eight workers.

The exact encoder path used 48 texture-transfer workers. It produced the same
stream SHA-256 as v14 for all three windows and passed 60 encoder/decoder
checksum comparisons with zero differences. Its mean stage changes were:

| Metric | MPEG v14 | Exact fork | Change |
| --- | ---: | ---: | ---: |
| Encode wall time | 128.653 s | 120.477 s | -6.35% |
| Attribute compression | 80.834 s | 73.783 s | -8.72% |
| Texture transfer | 12.658 s | 4.757 s | -62.42% |
| Peak encoder RSS | 554.78 MiB | 523.61 MiB | -5.62% |

## High-throughput encoder modes

The x265 start-window run used the `fast` preset, WPP enabled, four frame
threads, a 24-thread pool, one slice, and 48 texture-transfer workers.

| Metric, frames 1-10 | Threaded HM | x265 | Change |
| --- | ---: | ---: | ---: |
| Encode wall time | 119.71 s | 55.61 s | -53.55% |
| Attribute compression | 73.433 s | 7.986 s | -89.12% |
| Total stream bytes | 112,021 | 119,157 | +6.37% |
| Peak RSS | 536,500 KiB | 535,776 KiB | -0.13% |

The x265 stream decoded with 20 checksum comparisons equal and zero different.
All ten decoded OBJ geometry files matched the HM run by SHA-256. The decoded
texture-atlas comparison produced 22.86 dB aggregate RGB PSNR and 23.64 dB mean
per-frame PSNR relative to HM. D1, D2, PCQM, and source-referenced texture
metrics are still missing; the repository must not claim equal rate-distortion
performance from this timing result.

Independent GOF scheduling was tested with two three-frame GOFs. Wall time fell
from 33.75 to 17.12 seconds while total encoded bytes stayed at 102,796 and all
12 checksum comparisons were equal. This improves aggregate throughput when
GOFs have no reference dependency; it does not reduce single-GOF latency.

## Decoder results

Reference full-output decoding was dominated by serial color conversion and
OBJ/PNG/MTL creation. The optimized decoder assigns independent frames through
bounded worker pools.

On the same x265 ten-frame stream, matched five-run means were:

| Mode | Wall time | Peak RSS |
| --- | ---: | ---: |
| Serial conversion and serial output | 1.844 s | 218 MiB |
| Parallel conversion only | 1.554 s | 438 MiB |
| Parallel output only | 1.032 s | 238 MiB |
| Parallel conversion and output | 0.724 s | 434 MiB |

The combined path is 2.55x faster but uses about twice the peak memory. Across
the matrix, 100 decoder checks were equal, none differed, and all 30 output
files matched the serial decoder by SHA-256.

## Conclusions and next measurements

The best no-quality-change improvements are row-parallel texture transfer,
bounded decoder conversion/output workers, and parallel independent GOFs. The
x265 adapter delivers the largest encoder speedup, but it trades bitrate and
texture reconstruction against time and needs rate-distortion validation.

The next performance work should target preprocessing mesh fitting/UVAtlas and
base-mesh deformation, which become the main costs after x265. Before enabling
the x265 mode as a default, run a public licensed corpus with at least D1, D2,
PCQM, source-referenced texture PSNR/SSIM, bitrate, five measured runs after a
warm-up, and confidence intervals.

## Reproduction outline

Initialize both complete source trees:

```bash
git submodule update --init --recursive \
  open4d/codecs/vdmc open4d/codecs/faster_vdmc
```

Build the reference normally. To include the opt-in x265 adapter in the fork,
configure it with `-DUSE_X265_VIDEO_CODEC=ON`. Reproduce the configuration and
input files identified by the hashes in the JSON manifest, then invoke the
fork encoder with the settings recorded above. Use `/usr/bin/time -v` around
the encoder and decoder, retain their built-in stage timing output, hash every
bitstream and decoded file, and keep raw runs outside the Git checkout.
