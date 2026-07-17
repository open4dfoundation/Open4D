# Basketball 4D codec comparison

A simple single-page Streamlit app that compares the N4MC, QNDF, TVMC, and TSMC
decoded outputs of the 10-frame `basketball_player` sequence (fr0011–fr0020)
against the source, as looping animations. One toggle switches every panel to an
error-heatmap view (decoded-to-source surface distance on a shared scale).

The four codecs are **not** run by the app; it displays their already-decoded
meshes found under `open4d/modules/*/outputs/`. `prepare.py` renders those meshes
into the GIFs and metrics the app shows.

## Run

```bash
cd /home/ryan/Open4D/basketball_app
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt

python prepare.py                     # one-time: build assets/ and metrics.json
python -m streamlit run app.py --server.address 0.0.0.0 --server.port 8501
```

Set `OPEN4D_ROOT` if the checkout is not at `/home/ryan/Open4D`.

## Layout

- `prepare.py` — renders each sequence to `assets/<method>.gif` (shaded) and
  `assets/<method>_heat.gif` (error), writes `metrics.json` and `colorbar.png`.
  `python prepare.py --test` renders one shaded + one heatmap PNG to check the
  camera before the full build.
- `app.py` — the single-page viewer (five animations + a heatmap toggle + a
  metrics table).
- `assets/` — generated renders (gitignored).

Meshes are decimated to ~20k faces only for rendering; metrics are computed on
the full-resolution decoded meshes. Decoded size is the on-disk mesh file, not a
codec bitstream.
