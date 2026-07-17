"""Basketball 4D mesh codec comparison — a simple single-page Streamlit app.

Shows the source and the N4MC, QNDF, TVMC, TSMC, Draco, and KLT decoded
sequences side by side as looping animations, with one toggle to switch to an
error-heatmap view. Assets are built once by prepare.py.
"""
import json
from pathlib import Path

import streamlit as st

APP = Path(__file__).resolve().parent
ASSETS = APP / "assets"
METHODS = ["N4MC", "QNDF", "TVMC", "TSMC", "Draco", "KLT"]

st.set_page_config(page_title="Basketball 4D codec comparison", layout="wide")
st.title("Basketball player · 4D mesh codec comparison")
st.caption("N4MC, QNDF, TVMC, TSMC, Draco, and KLT decoded from the 10-frame "
           "basketball_player sequence (fr0011–fr0020).")

if not (ASSETS / "reference.gif").exists():
    st.warning("Assets not built yet. Run:  `python prepare.py`  then reload.")
    st.stop()

heat = st.toggle(
    "Show error heatmap",
    value=False,
    help="Colour each decoded surface by its distance to the source mesh, on a shared scale.",
)

columns = st.columns(len(METHODS) + 1)
with columns[0]:
    st.markdown("**Reference** · source")
    st.image(str(ASSETS / "reference.gif"), use_container_width=True)
for column, method in zip(columns[1:], METHODS):
    with column:
        st.markdown(f"**{method}**")
        name = f"{method.lower()}_heat.gif" if heat else f"{method.lower()}.gif"
        st.image(str(ASSETS / name), use_container_width=True)

if heat and (ASSETS / "colorbar.png").exists():
    st.image(str(ASSETS / "colorbar.png"), use_container_width=True)

metrics_file = APP / "metrics.json"
if metrics_file.exists():
    metrics = json.loads(metrics_file.read_text())
    st.subheader("Sequence-averaged surface metrics")
    st.dataframe(
        {
            "Method": METHODS,
            "Chamfer NRMSE (%)": [metrics[m]["chamfer_nrmse_pct"] for m in METHODS],
            "P95 distance (%)": [metrics[m]["p95_pct"] for m in METHODS],
            "Decoded faces": [int(metrics[m]["faces"]) for m in METHODS],
            "Decoded size (KB)": [metrics[m]["decoded_kb"] for m in METHODS],
            "Compressed size (KB)": [metrics[m].get("compressed_kb", "—") for m in METHODS],
        },
        hide_index=True,
        use_container_width=True,
    )
    st.caption("Distances are % of the source bounding-box diagonal (lower is better). "
               "Decoded size is the on-disk mesh file. Compressed size is the actual "
               "codec bitstream where available (Draco .drc; KLT per-frame coefficients "
               "+ codebook, excluding the amortized basis); “—” means the codec does not "
               "expose a comparable per-frame bitstream here. KLT is reconstructed from a "
               "128³ TSDF and aligned to the source, so it is resampled geometry.")
