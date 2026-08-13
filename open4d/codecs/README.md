# Codecs

This directory contains Open4D's compression implementations and reference
codec integrations. Each codec owns its environment, build instructions, data,
and generated outputs; consult the codec's README before running it.

The directory names are stable, lowercase identifiers:

- `draco`, `klt`, `n4mc`, `qndf`, and `qndf_int8`
- `tsmc` and `tvmc`
- `vdmc`, the pinned MPEG V-DMC test-model submodule

These implementations are not imported by the lightweight `open4d` package and
their heavyweight dependencies remain optional.

Gaussian-splatting compression is not here. QUEEN's quantization and 3DGStream's
neural transformation cache are inseparable from the training loops that produce
them, so both live in `../reconstruction/` alongside the reconstruction they are
part of.
