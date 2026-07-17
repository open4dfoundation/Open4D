# Open4D benchmark dashboard

This Streamlit app can upload a triangle OBJ and run N4MC, QNDF, TVMC, and
TSMC on an Open4D workstation. N4MC and QNDF train or optimize per upload.
TVMC and TSMC receive ten identical copies of the uploaded frame, as required
by their sequence pipelines. A completed same-input bunny run is bundled as a
read-only four-codec demo.

## Run

```bash
cd /path/to/Open4D/benchmark_app
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python -m streamlit run app.py
```

The runner expects the research-module environments and native tools described
by the module READMEs to already be installed. `OPEN4D_ROOT` defaults to
`/home/ryan/Open4D`; set it when the checkout lives elsewhere:

```bash
OPEN4D_ROOT=/path/to/Open4D python -m streamlit run app.py
```

## Bundled demo

The demo uses the original input and final N4MC, QNDF, TVMC, and TSMC meshes
from archived job `20260715_123756_aa298a49`. Its manifest is
`benchmark.json`; the five triangulated meshes live under `data/`.

The app computes the same surface-sampling metrics for every method. Only enter
`encoded_bytes` when a real, complete coded artifact was measured. Native rate
proxies and estimates belong in `variant` or `size_status`, not in the encoded
byte column.

## Job runner

The app creates isolated jobs under `runs/<job-id>`, executes the four methods
sequentially, preserves a separate log for each method, and restores decoded
meshes to the upload's original coordinate system. Dashboard data, outputs,
runs, and logs are local artifacts and are ignored by Git.

QNDF continues to use the original SSP remesher. Non-manifold meshes that SSP cannot process are reported as failed; the runner does not replace SSP or repair the upload silently.

## Important benchmark note

The bundled QNDF result uses SSP with `cs3000/ns2` and 300 epochs. The dashboard
shows decoded OBJ sizes separately from compressed sizes because an OBJ file is
not a codec bitstream. The QNDF size shown in its variant label is an estimate,
not a loadable coded stream.

Running TVMC and TSMC on ten duplicate frames verifies integration but does not
measure temporal compression on changing geometry. Do not present dashboard
results as a rate-distortion comparison unless every method uses the same
source sequence, frame range, geometry normalization, metric implementation,
and measured encoded byte count. TSMC's optional headless Open3D evaluation may
fail after decoding; the shared dashboard metrics remain available when that
happens.
