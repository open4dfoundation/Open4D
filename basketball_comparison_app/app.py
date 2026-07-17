from __future__ import annotations

import base64
import json
from pathlib import Path

import pandas as pd
import plotly.express as px
import streamlit as st


ROOT = Path(__file__).resolve().parent
COLORS = {"N4MC": "#F6C453", "QNDF": "#FF746C", "TVMC": "#31C6A5", "TSMC": "#9A7DFF"}
METHOD_ORDER = ("N4MC", "TSMC", "TVMC", "QNDF")


@st.cache_data(show_spinner=False)
def load_comparison(mtime: int) -> dict:
    del mtime
    return json.loads((ROOT / "comparison.json").read_text())


def metric_card(label: str, value: str, caption: str) -> None:
    st.markdown(
        f"<div class='metric'><span>{label}</span><strong>{value}</strong><small>{caption}</small></div>",
        unsafe_allow_html=True,
    )


@st.cache_data(show_spinner=False)
def encoded_video(path: str, mtime: int) -> str:
    del mtime
    return base64.b64encode(Path(path).read_bytes()).decode("ascii")


def looping_video(path: Path) -> None:
    payload = encoded_video(str(path), path.stat().st_mtime_ns)
    st.markdown(
        f'<video autoplay loop muted playsinline controls preload="auto" '
        f'style="display:block;width:100%;border-radius:14px;background:#08101d">'
        f'<source src="data:video/mp4;base64,{payload}" type="video/mp4"></video>',
        unsafe_allow_html=True,
    )


st.set_page_config(page_title="Open4D · Basketball sequence", page_icon="◈", layout="wide")
st.markdown("""
<style>
  .stApp { background:#07101c; color:#edf3fb; }
  .block-container { max-width:1500px; padding-top:1.7rem; }
  h1,h2,h3 { letter-spacing:-.035em; }
  .eyebrow { color:#55d9bd; font-size:.74rem; font-weight:800; letter-spacing:.16em; text-transform:uppercase; }
  .subtitle { color:#9aabc1; font-size:1.02rem; max-width:980px; margin:.3rem 0 1.5rem; }
  .metric { background:linear-gradient(145deg,#111e30,#0b1523); border:1px solid #263750; border-radius:15px; padding:1rem 1.1rem; min-height:112px; }
  .metric span { display:block; color:#8fa2ba; font-size:.72rem; letter-spacing:.08em; text-transform:uppercase; }
  .metric strong { display:block; font-size:1.55rem; margin:.25rem 0; }
  .metric small { color:#6f839e; }
  [data-testid="stVideo"] { border:1px solid #263750; border-radius:14px; overflow:hidden; background:#08101d; }
  .method { margin-top:.35rem; padding:.55rem .75rem; border-left:3px solid var(--c); background:#0d1827; border-radius:5px 10px 10px 5px; }
  .method b { font-size:1.05rem; } .method span { color:#8295ae; font-size:.82rem; display:block; }
  .heatmap-key { display:flex; align-items:center; gap:.7rem; color:#91a3ba; font-size:.78rem; margin:-.25rem 0 .8rem; }
  .heatmap-ramp { width:min(420px,55vw); height:11px; border-radius:999px; background:linear-gradient(90deg,#30123b,#466be3,#1ae4b6,#a4fc3c,#f9ba38,#e0442e,#7a0403); }
  hr { border-color:#1c2a3d !important; }
</style>
""", unsafe_allow_html=True)

manifest_path = ROOT / "comparison.json"
if not manifest_path.exists():
    st.error("Comparison assets have not been prepared yet.")
    st.stop()
data = load_comparison(manifest_path.stat().st_mtime_ns)

st.markdown("<div class='eyebrow'>Open4D · full-sequence benchmark</div>", unsafe_allow_html=True)
st.title("Basketball player · four-codec comparison")
st.markdown(
    "<div class='subtitle'>Ten source frames, fixed camera, identical playback rate, and one shared sampled-surface metric pass. "
    "Native rates are shown as context and are not ranked across codecs because their accounting differs.</div>",
    unsafe_allow_html=True,
)

aggregates = {name: value["aggregate"] for name, value in data["methods"].items()}
best_geometry = min(aggregates, key=lambda name: aggregates[name]["chamfer_nrmse_pct"])
best_normals = max(aggregates, key=lambda name: aggregates[name]["normal_consistency"])
cols = st.columns(4)
with cols[0]: metric_card("Sequence", "10 frames", "fr0011–fr0020")
with cols[1]: metric_card("Lowest normalized RMSE", best_geometry, f"{aggregates[best_geometry]['chamfer_nrmse_pct']:.4f}%")
with cols[2]: metric_card("Best normal consistency", best_normals, f"{aggregates[best_normals]['normal_consistency']:.4f}")
with cols[3]: metric_card("Surface samples", f"{data['samples']:,}", "per direction · per frame")

st.subheader("Synchronized output videos")
visualization = st.radio("Visualization", ("Surface", "Error heatmap"), horizontal=True)
show_heatmap = visualization == "Error heatmap"
if show_heatmap:
    heatmap = data["heatmap"]
    st.markdown(
        f"<div class='heatmap-key'><span>0%</span><div class='heatmap-ramp'></div>"
        f"<span>≥ {heatmap['max_pct']:.1f}%</span><span>distance from source surface</span></div>",
        unsafe_allow_html=True,
    )
video_columns = st.columns(4)
for index, name in enumerate(METHOD_ORDER):
    method = data["methods"][name]
    with video_columns[index]:
        video_key = "heatmap_video" if show_heatmap else "video"
        looping_video(ROOT / method[video_key])
        st.markdown(
            f"<div class='method' style='--c:{COLORS[name]}'><b>{name}</b><span>{method['variant']}</span></div>",
            unsafe_allow_html=True,
        )
        native = data["native_context"][name]
        st.caption(f"{native['label']}: {native['value']}")

st.divider()
st.subheader("Shared surface metrics · sequence mean")
rows = [{"Method": name, **metrics} for name, metrics in aggregates.items()]
aggregate_df = pd.DataFrame(rows)
left, right = st.columns(2)
with left:
    figure = px.bar(aggregate_df, x="Method", y="chamfer_nrmse_pct", color="Method",
                    color_discrete_map=COLORS, labels={"chamfer_nrmse_pct": "Symmetric normalized RMSE (%)"})
    figure.update_layout(showlegend=False, paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="rgba(0,0,0,0)")
    st.plotly_chart(figure, width="stretch")
with right:
    figure = px.bar(aggregate_df, x="Method", y="normal_consistency", color="Method",
                    color_discrete_map=COLORS, labels={"normal_consistency": "Normal consistency"})
    figure.update_layout(showlegend=False, paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="rgba(0,0,0,0)")
    st.plotly_chart(figure, width="stretch")

frame_names = [frame["frame"] for frame in data["frames"]]
selected = st.select_slider("Inspect a frame", options=frame_names)
frame = next(value for value in data["frames"] if value["frame"] == selected)
frame_rows = [{"Method": name, **metrics} for name, metrics in frame["methods"].items()]
frame_df = pd.DataFrame(frame_rows)
st.dataframe(
    frame_df[["Method", "chamfer_nrmse_pct", "p95_distance_pct", "sampled_hausdorff_pct",
              "normal_consistency", "vertices", "faces", "decoded_bytes"]].rename(columns={
        "chamfer_nrmse_pct": "Normalized RMSE (%)", "p95_distance_pct": "P95 distance (%)",
        "sampled_hausdorff_pct": "Sampled max (%)", "normal_consistency": "Normal consistency",
        "vertices": "Vertices", "faces": "Faces", "decoded_bytes": "Decoded bytes",
    }),
    hide_index=True,
    width="stretch",
)

with st.expander("Metric and playback methodology"):
    st.markdown(
        "- Every video uses the same global source bounds, orthographic camera, resolution, and 3 fps frame cadence.\n"
        "- Metrics use bidirectional surface samples and nearest-neighbor correspondences in original coordinates.\n"
        "- Heatmaps show decoded-vertex distance to the ground-truth surface as a percentage of that frame's source bounding-box diagonal. All methods use the same clipped scale.\n"
        "- Normal consistency uses the absolute dot product, making it robust to flipped mesh winding.\n"
        "- ‘Sampled max’ is not an exact Hausdorff distance. Native codec rate values are displayed but not directly compared."
    )
