# Security policy

Open4D is active research software and has not published a supported stable
release. It includes experimental network receivers, native decoders, GPU
extensions, model-loading paths, and third-party research systems. Do not
expose them directly to hostile networks or untrusted artifacts.

## Report a vulnerability

Report vulnerabilities privately through the repository's
[GitHub security advisory form](https://github.com/open4dfoundation/Open4D/security/advisories/new).
Do not open a public issue for a vulnerability that could put users, devices,
data, or infrastructure at risk.

Include the affected revision and component, prerequisites, reproduction
steps, impact, and proposed mitigation. Remove credentials, device serials,
private data, and identifying capture content.

## Deployment guidance

- Treat geometry, archives, models, checkpoints, and configurations as
  untrusted input; parse them with resource limits and least privilege.
- Bind experimental receivers to localhost by default and use SSH tunneling
  for remote experiments.
- Isolate native decoders, Unity plugins, CUDA extensions, and copied research
  code.
- Fetch external artifacts only through immutable, checksum-verified
  references.
- Revoke exposed credentials before removing them from Git history.

## Known limitations

O4D framing and hashes validate container integrity; they do not authenticate
the publisher or make native payloads safe to execute. Inspection, packing,
and compressed USD round trips do not invoke native models. Decoding can:

- [QUEEN's compressed loader](open4d/reconstruction/queen/scene/gaussian_model.py)
  uses `pickle.load`, which can execute code from a malicious residual.
- [ReRF's player](open4d/reconstruction/rerf/rerf_stream/bitstream.py) loads
  checkpoints with `torch.load` without explicitly selecting `weights_only`;
  older Torch runtimes can execute serialized code. Other research entry points
  also load executable checkpoints. A worker subprocess is not a security sandbox.

The [Unity TVMC backend](integrations/unity/TVMCUnity/CPP_Backend/src) has
unchecked OBJ face indices, a decoder access to row 100 without a minimum-row
check, and a `FetchFrame` interface without an output-capacity argument.
Its matrix loader also permits very large allocations before checking that all
declared bytes exist. These are source-level memory safety and resource risks;
runtime exploitation and sanitizer validation have not been performed here.

The Node control API, decoded-frame TCP transport, and research WebSocket
bridges have no authentication. Origin and Host checks do not authenticate
native clients. Some bridges bind to all interfaces, and static file routes
follow symlinks; their network exposure and asset roots must be controlled.

Vendored QUEEN and 3DGStream trainer/renderer argument loaders evaluate
`cfg_args`. Some 3DGStream conversion scripts and the ReRF LLFF loader construct
shell commands from paths or options. Use trusted inputs for those entry points;
the O4D import path uses JSON instead of these evaluated configurations.

Legacy ZIP/NPZ paths do not consistently bound aggregate expanded bytes.
Repacking a legacy N4MC archive can exhaust disk before O4D validation, and
neural architecture or volume dimensions can trigger large allocations even
after safe deserialization.
