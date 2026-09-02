# Vendored Draco decoder

`draco_wasm_wrapper.js` and `draco_decoder.wasm`, copied from
`open4d/codecs/tsmc/draco/javascript/` — the same Draco build the codecs in this
repository already vendor, so the encoder and this decoder are one version.

Copied rather than referenced across trees for two reasons. The client is a
package that has to be servable on its own, and pointing at a path inside
another codec's vendored checkout would break the moment that checkout moved or
was cleaned. And it is served from this origin, never a CDN, which is what keeps
the page free of external dependencies.

Upstream: https://github.com/google/draco (Apache-2.0; see the codec tree for
the licence text). Refresh both files together — the wrapper and the module are
a matched pair.
