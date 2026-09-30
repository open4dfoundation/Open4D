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

The absence of a warning is not a claim that a component is production-ready.
