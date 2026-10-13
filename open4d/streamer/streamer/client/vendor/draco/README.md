# Vendored Draco decoder

`draco_wasm_wrapper.js` and `draco_decoder.wasm`, copied from
`open4d/codecs/tsmc/draco/javascript/` — the same Draco build the codecs in this
repository already vendor, so the encoder and this decoder are one version.

Copied rather than referenced across trees for two reasons. The client is a
package that has to be servable on its own, and pointing at a path inside
another codec's vendored checkout would break the moment that checkout moved or
was cleaned. And it is served from this origin, never a CDN, which is what keeps
the page free of external dependencies.

Upstream: [google/draco at 47238930f698250f474163e1a29d77858aa5c158](https://github.com/google/draco/tree/47238930f698250f474163e1a29d77858aa5c158).
The vendored pair was verified byte-for-byte against the `javascript/` files at
that immutable revision. Neither decoder file has Open4D modifications.

The exact upstream [LICENSE](LICENSE) and [AUTHORS](AUTHORS) are retained here.
Upstream has no root `NOTICE` at this revision. [NOTICE](NOTICE) is an
Open4D-authored attribution notice, using the copyright attribution in the
upstream decoder wrapper source, not a purported copy of an upstream notice.
The full upstream license file also retains its documentation-file notices;
those documentation assets are not part of this package.

SHA-256:

| File | Digest |
| --- | --- |
| `draco_wasm_wrapper.js` | `e8049906ef3f8f75d3456c22a3f31bfdfe5b5b5bd09ccdec613b9e9a49d554d8` |
| `draco_decoder.wasm` | `2516a4e43526d71787bf2f678f951329f7f858f8f15f42d4bc9e370b31a0da3a` |
| `LICENSE` | `d3709b0fb4b8a94bbb1d02b8a2e484f258b0d9c5c5a01f940391f3fe662cd1a4` |
| `AUTHORS` | `7399754545fa82fbf402e3e91d50de0b63772870fd807c48dd9af25c042efb10` |

Refresh both decoder files together: the wrapper and the module are a
matched pair. Update the immutable revision, accompanying notices and the digest check
in `scripts/check_streamer_package.py` whenever refreshing them. That checker
also rejects research-study content in the separately built wheel and sdist.
