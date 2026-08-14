# Open4D contributor handbook

The handbook is versioned because the repository contains both a small shared
platform and several fast-moving research implementations. A status statement
is useful only when it names the revision it describes.

## Available snapshots

| Snapshot | Repository revision | Audit date | State |
| --- | --- | --- | --- |
| [`v0.2-dev`](v0.2-dev/README.md) | `96b8c7bbb48e2a8d231684639cfc57799ca6666d` | 2026-08-13 | Current development baseline |

Start with the current snapshot's [orientation and learning path](v0.2-dev/README.md).
Then read its [current implementation status](v0.2-dev/implementation-status.md)
for the live gap between the audited baseline and the roadmap.
When `v0.2` is released, this directory should be copied to `v0.2/` and frozen;
future audits should create a new snapshot rather than rewriting history.

## How to read status claims

The handbook separates three different kinds of evidence:

1. **Recorded verification**: a command was run in the named environment and
   its result was captured by the audit.
2. **Source inspection**: the code and documentation contain a capability, but
   this audit did not reproduce it end to end.
3. **Upstream/research claim**: a paper or imported project describes a result;
   Open4D has not necessarily reproduced it.

The exact rubric, evidence, and component register are in
[`v0.2-dev/status.md`](v0.2-dev/status.md).
