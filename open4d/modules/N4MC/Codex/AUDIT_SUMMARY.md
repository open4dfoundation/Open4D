# Legacy Pipeline Audit

## Reusable

- [dataset.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/dataset.py): confirms historical `.npz` usage and channel ordering expectations.
- [fmc.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/fmc.py): reusable marching-cubes reference, but the new baseline currently uses `skimage.measure.marching_cubes` for a simpler evaluation path.
- [environment.yml](/home/frozzzen/Documents/Github/Implicit-mesh-compression/environment.yml): useful for expected runtime dependencies.

## Deprecated For The New Path

- [network.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/network.py): old quantized AE stack, tightly coupled to previous assumptions.
- [train_quant.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/train_quant.py): monolithic training loop with legacy loss design and no clean rate-distortion accounting.
- [decoder.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/decoder.py), [encoder.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/encoder.py), [loss.py](/home/frozzzen/Documents/Github/Implicit-mesh-compression/loss.py): historical path only.

## Migration Risks

- Legacy code mixed TSDF and offset channels; the new baseline is TSDF-only and assumes `offset` is ignored because stored offsets are zeros.
- Historical code used multiple implicit layout changes; the new path keeps tensors channel-first and validates shape/range on load.
- Existing evaluation utilities depend on heavier geometry libraries. The new baseline avoids those dependencies where possible, but mesh metrics still require the repo environment from [environment.yml](/home/frozzzen/Documents/Github/Implicit-mesh-compression/environment.yml).
