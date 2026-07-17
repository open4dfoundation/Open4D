from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import pandas as pd
import plotly.express as px
import streamlit as st
from scipy.spatial import cKDTree

from job_control import RUNS_ROOT, cancel_job, create_job, list_jobs, load_status


ROOT = Path(__file__).resolve().parent
MANIFEST_PATH = ROOT / "benchmark.json"
COLORS = {
    "TVMC": "#31C6A5",
    "N4MC": "#F6C453",
    "TSMC": "#9A7DFF",
    "QNDF": "#FF746C",
}


@dataclass(frozen=True)
class Mesh:
    vertices: np.ndarray
    faces: np.ndarray


def resolve_path(value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value).expanduser()
    return path if path.is_absolute() else ROOT / path


@st.cache_data(show_spinner=False)
def read_manifest(path: str, mtime_ns: int) -> dict[str, Any]:
    del mtime_ns
    return json.loads(Path(path).read_text())


@st.cache_data(show_spinner=False)
def load_obj(path: str, mtime_ns: int) -> Mesh:
    del mtime_ns
    vertices: list[list[float]] = []
    faces: list[list[int]] = []
    with Path(path).open("r", encoding="utf-8", errors="ignore") as handle:
        for raw_line in handle:
            if raw_line.startswith("v "):
                fields = raw_line.split()
                if len(fields) >= 4:
                    vertices.append([float(fields[1]), float(fields[2]), float(fields[3])])
            elif raw_line.startswith("f "):
                indices = []
                for field in raw_line.split()[1:]:
                    token = field.split("/", 1)[0]
                    if token:
                        index = int(token)
                        indices.append(index - 1 if index > 0 else len(vertices) + index)
                # Fan triangulation keeps the loader useful for quad/polygon OBJ files.
                for offset in range(1, len(indices) - 1):
                    faces.append([indices[0], indices[offset], indices[offset + 1]])
    if not vertices or not faces:
        raise ValueError(f"No triangle mesh data found in {path}")
    mesh = Mesh(np.asarray(vertices, dtype=np.float64), np.asarray(faces, dtype=np.int64))
    if mesh.faces.min() < 0 or mesh.faces.max() >= len(mesh.vertices):
        raise ValueError(f"OBJ face index is outside the vertex array in {path}")
    return mesh


def load_mesh(path: Path) -> Mesh:
    if path.suffix.lower() != ".obj":
        raise ValueError("This dashboard currently accepts triangulated OBJ files.")
    return load_obj(str(path), path.stat().st_mtime_ns)


@st.cache_data(show_spinner=False)
def sample_surface(path: str, mtime_ns: int, count: int, seed: int) -> tuple[np.ndarray, np.ndarray]:
    mesh = load_obj(path, mtime_ns)
    triangles = mesh.vertices[mesh.faces]
    cross = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
    double_area = np.linalg.norm(cross, axis=1)
    valid = double_area > np.finfo(np.float64).eps
    triangles = triangles[valid]
    cross = cross[valid]
    double_area = double_area[valid]
    if not len(triangles):
        raise ValueError(f"All triangles are degenerate in {path}")

    rng = np.random.default_rng(seed)
    selected = rng.choice(len(triangles), size=count, p=double_area / double_area.sum())
    chosen = triangles[selected]
    u = rng.random(count)
    v = rng.random(count)
    reflected = u + v > 1.0
    u[reflected] = 1.0 - u[reflected]
    v[reflected] = 1.0 - v[reflected]
    points = chosen[:, 0] + u[:, None] * (chosen[:, 1] - chosen[:, 0]) + v[:, None] * (chosen[:, 2] - chosen[:, 0])
    normals = cross[selected]
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)
    return points, normals


@st.cache_data(show_spinner=False)
def comparable_metrics(
    reference_path: str,
    reference_mtime: int,
    output_path: str,
    output_mtime: int,
    samples: int,
) -> dict[str, float]:
    reference = load_obj(reference_path, reference_mtime)
    ref_points, ref_normals = sample_surface(reference_path, reference_mtime, samples, 20260715)
    out_points, out_normals = sample_surface(output_path, output_mtime, samples, 20260716)

    out_tree = cKDTree(out_points)
    ref_tree = cKDTree(ref_points)
    ref_to_out, ref_nearest = out_tree.query(ref_points, workers=-1)
    out_to_ref, out_nearest = ref_tree.query(out_points, workers=-1)
    all_distances = np.concatenate([ref_to_out, out_to_ref])
    diagonal = float(np.linalg.norm(np.ptp(reference.vertices, axis=0)))
    normal_dots = np.concatenate(
        [
            np.abs(np.einsum("ij,ij->i", ref_normals, out_normals[ref_nearest])),
            np.abs(np.einsum("ij,ij->i", out_normals, ref_normals[out_nearest])),
        ]
    )
    return {
        "chamfer_mse": float(np.mean(ref_to_out**2) + np.mean(out_to_ref**2)),
        "chamfer_nrmse_pct": float(np.sqrt(np.mean(all_distances**2)) / diagonal * 100),
        "p95_distance_pct": float(np.percentile(all_distances, 95) / diagonal * 100),
        "hausdorff_pct": float(np.max(all_distances) / diagonal * 100),
        "normal_consistency": float(np.mean(normal_dots)),
    }


def surface_area(mesh: Mesh) -> float:
    triangles = mesh.vertices[mesh.faces]
    return float(
        np.linalg.norm(
            np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0]),
            axis=1,
        ).sum()
        / 2
    )


def mesh_summary(path: Path, mesh: Mesh) -> dict[str, Any]:
    return {
        "vertices": len(mesh.vertices),
        "faces": len(mesh.faces),
        "surface_area": surface_area(mesh),
        "decoded_obj_bytes": path.stat().st_size,
        "bbox_diagonal": float(np.linalg.norm(np.ptp(mesh.vertices, axis=0))),
    }


@st.cache_data(show_spinner=False)
def mesh_web_payload(path: str, mtime_ns: int) -> tuple[str, str]:
    mesh = load_obj(path, mtime_ns)
    # Three.js receives every triangle. The previous Plotly preview sampled
    # faces, which made intact meshes look as if pieces of their surface were missing.
    positions = json.dumps(mesh.vertices.astype(np.float32).ravel().tolist(), separators=(",", ":"))
    indices = json.dumps(mesh.faces.astype(np.uint32).ravel().tolist(), separators=(",", ":"))
    return positions, indices


def mesh_viewer(path: Path, color: str, height: int = 360) -> None:
    positions, indices = mesh_web_payload(str(path), path.stat().st_mtime_ns)
    st.iframe(
        f"""
        <div id="viewer"><div class="hint">drag to rotate · scroll to zoom · right-drag to pan</div></div>
        <style>
          html,body,#viewer {{ margin:0; width:100%; height:100%; overflow:hidden; background:#0d1624; border-radius:12px; }}
          .hint {{ position:absolute; z-index:2; left:10px; bottom:8px; color:#8091aa; font:11px system-ui; pointer-events:none; }}
          canvas {{ display:block; }}
        </style>
        <script type="importmap">{{"imports":{{"three":"https://cdn.jsdelivr.net/npm/three@0.161.0/build/three.module.js"}}}}</script>
        <script type="module">
          import * as THREE from 'three';
          import {{ OrbitControls }} from 'https://cdn.jsdelivr.net/npm/three@0.161.0/examples/jsm/controls/OrbitControls.js';
          const host = document.getElementById('viewer');
          const scene = new THREE.Scene();
          scene.background = new THREE.Color(0x0d1624);
          const camera = new THREE.PerspectiveCamera(38, host.clientWidth / host.clientHeight, 0.001, 10000);
          const renderer = new THREE.WebGLRenderer({{antialias:true, alpha:false, powerPreference:'high-performance'}});
          renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
          renderer.setSize(host.clientWidth, host.clientHeight);
          renderer.outputColorSpace = THREE.SRGBColorSpace;
          host.appendChild(renderer.domElement);
          const geometry = new THREE.BufferGeometry();
          geometry.setAttribute('position', new THREE.Float32BufferAttribute({positions}, 3));
          geometry.setIndex(new THREE.Uint32BufferAttribute({indices}, 1));
          geometry.computeVertexNormals();
          geometry.computeBoundingBox();
          geometry.computeBoundingSphere();
          const material = new THREE.MeshStandardMaterial({{
            color:new THREE.Color('{color}'), roughness:0.72, metalness:0.02, side:THREE.DoubleSide
          }});
          const mesh = new THREE.Mesh(geometry, material);
          scene.add(mesh);
          scene.add(new THREE.HemisphereLight(0xdceaff, 0x172235, 2.2));
          const key = new THREE.DirectionalLight(0xffffff, 2.5); key.position.set(2,-3,4); scene.add(key);
          const fill = new THREE.DirectionalLight(0x8db6ff, 1.1); fill.position.set(-3,2,1); scene.add(fill);
          const center = geometry.boundingSphere.center;
          const radius = Math.max(geometry.boundingSphere.radius, 0.001);
          camera.position.copy(center).add(new THREE.Vector3(1.25,-1.65,0.8).normalize().multiplyScalar(radius * 3.0));
          camera.near = radius / 1000; camera.far = radius * 1000; camera.updateProjectionMatrix();
          const controls = new OrbitControls(camera, renderer.domElement);
          controls.target.copy(center); controls.enableDamping=true; controls.dampingFactor=.08; controls.update();
          function resize() {{
            const w=host.clientWidth, h=host.clientHeight;
            renderer.setSize(w,h,false); camera.aspect=w/h; camera.updateProjectionMatrix();
          }}
          new ResizeObserver(resize).observe(host);
          function render() {{ controls.update(); renderer.render(scene,camera); requestAnimationFrame(render); }}
          render();
        </script>
        """,
        height=height,
    )


def human_bytes(value: float | int | None) -> str:
    if value is None:
        return "—"
    number = float(value)
    for unit in ("B", "KB", "MB", "GB"):
        if number < 1024 or unit == "GB":
            return f"{number:.1f} {unit}"
        number /= 1024
    return "—"


def metric_card(label: str, value: str, caption: str = "") -> None:
    st.markdown(
        f"<div class='metric-card'><span>{label}</span><strong>{value}</strong><small>{caption}</small></div>",
        unsafe_allow_html=True,
    )


st.set_page_config(page_title="Open4D · Codec benchmark", page_icon="◈", layout="wide")
st.markdown(
    """
    <style>
      .stApp { background: #08101d; color: #e7edf7; }
      [data-testid="stSidebar"] { background: #0d1726; border-right: 1px solid #233149; }
      .block-container { max-width: 1500px; padding-top: 2rem; }
      h1, h2, h3 { letter-spacing: -0.035em; }
      .eyebrow { color: #57d9bc; font-size: .75rem; font-weight: 700; letter-spacing: .16em; text-transform: uppercase; }
      .subtitle { color: #9cabc0; max-width: 880px; font-size: 1.05rem; margin-bottom: 1.5rem; }
      .metric-card { background: linear-gradient(145deg,#111d2e,#0c1523); border: 1px solid #26364e; border-radius: 14px; padding: 1rem 1.1rem; min-height: 110px; }
      .metric-card span { display:block; color:#8fa1b9; font-size:.76rem; text-transform:uppercase; letter-spacing:.08em; }
      .metric-card strong { display:block; color:#f1f5fb; font-size:1.65rem; margin:.22rem 0; }
      .metric-card small { color:#687b96; }
      .status-ready { color:#57d9bc; font-weight:700; }
      .status-missing { color:#f6c453; font-weight:700; }
      [data-testid="stDataFrame"] { border: 1px solid #26364e; border-radius: 12px; overflow: hidden; }
      .stTabs [data-baseweb="tab-list"] { gap: 1.6rem; }
      .stTabs [data-baseweb="tab"] { padding-left: 0; padding-right: 0; }
    </style>
    """,
    unsafe_allow_html=True,
)

jobs = [job for job in list_jobs() if job.get("state") != "cancelled"]
job_labels = {f"{job['name']} · {job['job_id']} · {job['state']}": job["job_id"] for job in jobs}
active_jobs = [job for job in jobs if job.get("state") in {"queued", "running"}]

st.sidebar.markdown("### Run a mesh")
with st.sidebar.expander("New four-codec benchmark", expanded=False):
    upload = st.file_uploader("Triangle OBJ", type=["obj"], accept_multiple_files=False)
    n4mc_epochs = st.number_input("N4MC epochs", min_value=10, max_value=4000, value=300, step=10)
    qndf_epochs = st.number_input("QNDF epochs", min_value=10, max_value=4000, value=300, step=10)
    qndf_col1, qndf_col2 = st.columns(2)
    with qndf_col1:
        qndf_coarse = st.number_input("QNDF coarse faces", min_value=500, max_value=50_000, value=3000, step=500)
    with qndf_col2:
        qndf_subdiv = st.number_input("Subdivisions", min_value=1, max_value=4, value=2, step=1)
    st.caption("TVMC and TSMC receive ten identical copies. Jobs run sequentially to avoid GPU contention.")
    if active_jobs:
        active = active_jobs[0]
        st.info(f"A benchmark is already {active['state']}: {active['name']} · {active['job_id']}. Cancel it before starting another.")
    if st.button("Start remote benchmark", type="primary", disabled=upload is None or bool(active_jobs), width="stretch"):
        payload = upload.getvalue() if upload else b""
        if len(payload) > 200 * 1024 * 1024:
            st.error("Upload exceeds the 200 MB limit.")
        elif b"\nv " not in b"\n" + payload or b"\nf " not in b"\n" + payload:
            st.error("The OBJ must contain vertex and face records.")
        else:
            job_id = create_job(
                upload.name,
                payload,
                {
                    "n4mc_epochs": int(n4mc_epochs),
                    "n4mc_resolution": 127,
                    "n4mc_preprocess_iterations": 500,
                    "qndf_epochs": int(qndf_epochs),
                    "qndf_coarse_size": int(qndf_coarse),
                    "qndf_subdivisions": int(qndf_subdiv),
                    "tsmc_eigenvectors": 5,
                },
            )
            st.session_state["selected_job"] = job_id
            st.rerun()

jobs = [job for job in list_jobs() if job.get("state") != "cancelled"]
job_labels = {f"{job['name']} · {job['job_id']} · {job['state']}": job["job_id"] for job in jobs}
source_options = ["Four-codec bunny demo", *job_labels]
default_source = next(iter(job_labels), "Four-codec bunny demo")
selected_job_id = st.session_state.get("selected_job")
for label, job_id in job_labels.items():
    if job_id == selected_job_id:
        default_source = label
        break
source = st.sidebar.selectbox("Results", source_options, index=source_options.index(default_source))
current_job: dict[str, Any] | None = None

if source == "Four-codec bunny demo":
    manifest = read_manifest(str(MANIFEST_PATH), MANIFEST_PATH.stat().st_mtime_ns)
    dataset_name = manifest["reference"].get("name", "Stanford bunny")
else:
    current_job = load_status(job_labels[source])
    job_dir = RUNS_ROOT / current_job["job_id"]
    dataset_name = current_job["name"]
    manifest = {
        "reference": {"name": dataset_name, "mesh": str(job_dir / current_job["input"])},
        "methods": [],
    }
    for method_name in ("TVMC", "N4MC", "TSMC", "QNDF"):
        record = current_job["methods"][method_name]
        output = job_dir / record["output"] if record.get("output") else None
        manifest["methods"].append({
            "name": method_name,
            "variant": record.get("variant", "single-mesh adapter"),
            "mesh": str(output) if output else None,
            "encoded_bytes": record.get("encoded_bytes"),
            "size_status": record.get("size_status", "not measured"),
            "runner_state": record.get("state", "pending"),
            "runner_error": record.get("error"),
            "runner_warning": record.get("warning"),
        })

reference_path = resolve_path(manifest["reference"]["mesh"])
if reference_path is None or not reference_path.exists():
    st.error("Reference mesh is missing.")
    st.stop()

st.sidebar.markdown("### Benchmark controls")
sample_count = st.sidebar.select_slider(
    "Surface samples",
    options=[5_000, 10_000, 25_000, 50_000],
    value=25_000,
    help="More samples reduce Monte Carlo noise but take longer.",
)
st.sidebar.caption("Metrics are cached for each mesh and sample count.")
st.sidebar.divider()
st.sidebar.markdown(f"**Dataset**  \n{dataset_name}")
st.sidebar.markdown("**Evaluation**  \nSymmetric surface sampling")

reference_mesh = load_mesh(reference_path)
reference_summary = mesh_summary(reference_path, reference_mesh)
records: list[dict[str, Any]] = []
loaded_meshes: dict[str, Mesh] = {}
missing: list[str] = []

with st.spinner("Computing comparable surface metrics…"):
    for method in manifest["methods"]:
        path = resolve_path(method.get("mesh"))
        record: dict[str, Any] = {
            "Method": method["name"],
            "Variant": method.get("variant", ""),
            "Status": method.get("runner_state", "missing").title(),
            "Mesh path": str(path) if path else "Not configured",
            "Encoded bytes": method.get("encoded_bytes"),
            "Size status": method.get("size_status", "unknown"),
        }
        if path and path.exists():
            try:
                mesh = load_mesh(path)
                loaded_meshes[method["name"]] = mesh
                summary = mesh_summary(path, mesh)
                metrics = comparable_metrics(
                    str(reference_path),
                    reference_path.stat().st_mtime_ns,
                    str(path),
                    path.stat().st_mtime_ns,
                    sample_count,
                )
                record.update(summary)
                record.update(metrics)
                record["Status"] = "Ready"
            except Exception as error:
                record["Status"] = f"Error: {error}"
        elif method.get("runner_state") == "failed":
            record["Status"] = "Failed"
            record["Error"] = method.get("runner_error")
        else:
            missing.append(method["name"])
        records.append(record)

data = pd.DataFrame(records)
ready = data[data["Status"] == "Ready"].copy()

st.markdown("<div class='eyebrow'>Open4D codec study</div>", unsafe_allow_html=True)
st.title(f"{dataset_name} benchmark")
st.markdown(
    "<div class='subtitle'>One completed same-input run, four decoded meshes, and one shared geometry evaluation protocol. "
    "TVMC and TSMC use ten duplicated frames for this integration demo; blank compressed-size fields mean that a "
    "comparable bitstream was not retained.</div>",
    unsafe_allow_html=True,
)

status_columns = st.columns(4)
for column, method in zip(status_columns, manifest["methods"]):
    row = data[data["Method"] == method["name"]].iloc[0]
    with column:
        status_class = "status-ready" if row["Status"] == "Ready" else "status-missing"
        st.markdown(f"#### <span style='color:{COLORS[method['name']]}'>{method['name']}</span>", unsafe_allow_html=True)
        st.markdown(f"<span class='{status_class}'>{row['Status']}</span>", unsafe_allow_html=True)
        st.caption(method.get("variant", ""))

if missing and current_job is None:
    st.info(
        "Awaiting benchmark output: " + ", ".join(missing) + ". "
        "Add the triangulated OBJ files at the paths listed in benchmark.json and reload."
    )

if current_job is not None:
    with st.expander(f"Run details · {current_job['job_id']}", expanded=current_job["state"] in {"queued", "running"}):
        top_left, top_right = st.columns([4, 1])
        with top_left:
            st.caption(f"Overall state: {current_job['state']} · created {current_job['created_at']}")
        with top_right:
            if current_job["state"] in {"queued", "running"} and st.button("Cancel job", width="stretch"):
                cancel_job(current_job["job_id"])
                st.rerun()
        cols = st.columns(4)
        for column, method_name in zip(cols, ("N4MC", "QNDF", "TVMC", "TSMC")):
            method = current_job["methods"][method_name]
            with column:
                st.markdown(f"**{method_name}** · {method['state'].title()}")
                if method.get("elapsed_seconds") is not None:
                    st.caption(f"{method['elapsed_seconds']:.1f} seconds")
                if method.get("error"):
                    st.error(method["error"])
                if method.get("warning"):
                    st.warning(method["warning"])
        log_method = st.selectbox("Log", ["runner", "n4mc", "qndf", "tvmc", "tsmc"])
        log_path = RUNS_ROOT / current_job["job_id"] / f"{log_method}.log"
        log_text = log_path.read_text(encoding="utf-8", errors="replace")[-30_000:] if log_path.exists() else "No log output yet."
        if current_job["methods"]["N4MC"]["state"] == "running":
            n4mc_log = RUNS_ROOT / current_job["job_id"] / "n4mc.log"
            full_log = n4mc_log.read_text(encoding="utf-8", errors="replace") if n4mc_log.exists() else ""
            epochs = [int(value) for value in re.findall(r"epoch=(\d+)", full_log)]
            if epochs:
                target = int(current_job["settings"].get("n4mc_epochs", 300))
                current = min(max(epochs), target)
                st.progress(current / target, text=f"N4MC training: epoch {current} / {target}")
        st.code(log_text, language="text")
        if current_job["state"] in {"queued", "running"}:
            st.info("This page refreshes every 8 seconds while the job is active.")
            st.iframe("<script>setTimeout(() => window.parent.location.reload(), 8000)</script>", height=0)

st.subheader("Comparable results")
if ready.empty:
    st.warning("No reconstruction meshes are available.")
else:
    best_chamfer = ready.loc[ready["chamfer_nrmse_pct"].idxmin()]
    best_normal = ready.loc[ready["normal_consistency"].idxmax()]
    c1, c2, c3, c4 = st.columns(4)
    with c1:
        metric_card("Available methods", f"{len(ready)} / 4", "real decoded meshes")
    with c2:
        metric_card("Lowest normalized RMSE", best_chamfer["Method"], f"{best_chamfer['chamfer_nrmse_pct']:.4f}%")
    with c3:
        metric_card("Best normal consistency", best_normal["Method"], f"{best_normal['normal_consistency']:.5f}")
    with c4:
        metric_card("Reference triangles", f"{reference_summary['faces']:,}", human_bytes(reference_path.stat().st_size))

    chart_left, chart_right = st.columns(2)
    with chart_left:
        figure = px.bar(ready, x="Method", y="chamfer_nrmse_pct", color="Method", color_discrete_map=COLORS,
                        labels={"chamfer_nrmse_pct": "Normalized surface RMSE (%)"},
                        title="Geometric distortion · lower is better")
        figure.update_layout(showlegend=False, paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="#0d1624", font_color="#cdd7e5")
        st.plotly_chart(figure, width="stretch")
    with chart_right:
        figure = px.bar(ready, x="Method", y="normal_consistency", color="Method", color_discrete_map=COLORS,
                        range_y=[max(0, float(ready["normal_consistency"].min()) - 0.02), 1.0],
                        labels={"normal_consistency": "Normal consistency"},
                        title="Surface orientation · higher is better")
        figure.update_layout(showlegend=False, paper_bgcolor="rgba(0,0,0,0)", plot_bgcolor="#0d1624", font_color="#cdd7e5")
        st.plotly_chart(figure, width="stretch")

st.subheader("Decoded meshes · full surface")
st.caption("All four independent Three.js viewers render every triangle. Drag, zoom, and pan each reconstruction directly.")
method_lookup = {method["name"]: method for method in manifest["methods"]}
for method_pair in (("TVMC", "N4MC"), ("TSMC", "QNDF")):
    viewer_columns = st.columns(2, gap="large")
    for column, method_name in zip(viewer_columns, method_pair):
        with column:
            st.markdown(f"### <span style='color:{COLORS[method_name]}'>{method_name}</span>", unsafe_allow_html=True)
            method_path = resolve_path(method_lookup[method_name].get("mesh"))
            method_rows = ready[ready["Method"] == method_name]
            if method_path and method_path.exists() and not method_rows.empty:
                mesh_viewer(method_path, COLORS[method_name])
                row = method_rows.iloc[0]
                st.markdown(
                    f"""
                    **Surface RMSE:** {row['chamfer_nrmse_pct']:.5f}%  
                    **P95 distance:** {row['p95_distance_pct']:.5f}%  
                    **Sampled max:** {row['hausdorff_pct']:.5f}%  
                    **Normal consistency:** {row['normal_consistency']:.6f}  
                    **Vertices / triangles:** {int(row['vertices']):,} / {int(row['faces']):,}  
                    **Decoded OBJ:** {human_bytes(int(row['decoded_obj_bytes']))}  
                    **BBox diagonal:** {row['bbox_diagonal']:.6f}
                    """
                )
                st.caption(method_lookup[method_name].get("variant", ""))
            else:
                st.info("No decoded mesh available.")

display_columns = [
    "Method", "Variant", "Status", "chamfer_nrmse_pct", "p95_distance_pct", "hausdorff_pct",
    "normal_consistency", "vertices", "faces", "decoded_obj_bytes", "Encoded bytes", "Size status",
]
for column in display_columns:
    if column not in data:
        data[column] = np.nan
formatted = data[display_columns].rename(columns={
    "chamfer_nrmse_pct": "Surface RMSE (% diag)", "p95_distance_pct": "P95 distance (% diag)",
    "hausdorff_pct": "Sampled max (% diag)", "normal_consistency": "Normal consistency",
    "vertices": "Vertices", "faces": "Triangles", "decoded_obj_bytes": "Decoded OBJ bytes",
})
st.subheader("Complete metric table")
st.dataframe(formatted, hide_index=True, width="stretch")
st.download_button("Download metrics CSV", ready.to_csv(index=False).encode(),
                   file_name=f"open4d_benchmark_{sample_count}_samples.csv", mime="text/csv", disabled=ready.empty)
st.caption("Blank cells mean not measured—not zero. Decoded OBJ size is not compressed bitstream size.")

with st.expander("Protocol and fairness caveats"):
    st.markdown(
        f"""
        Each decoded mesh is sampled at **{sample_count:,} area-weighted surface points**. KD-trees find nearest samples in
        both directions. Distances are divided by the reference bounding-box diagonal; seeds are fixed and results cached.

        - **Surface RMSE, P95 distance, and sampled max:** lower is better.
        - **Normal consistency:** higher is better; 1 is ideal.
        - TVMC and TSMC use ten duplicated frames; this page compares decoded frame zero, not temporal stability.
        - QNDF optimizes a network per mesh, so training time is encoding time.
        - Uploaded QNDF jobs use the original SSP remesher and preserve an SSP rejection rather than changing remeshers.
        - Every runner receives the same centered, uniformly scaled input, and outputs are restored to original coordinates.
        """
    )
