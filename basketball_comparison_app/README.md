# Basketball sequence comparison

This Streamlit app compares the complete ten-frame basketball outputs from
N4MC, QNDF, TVMC, and TSMC with synchronized fixed-camera videos and shared
surface metrics. A synchronized error-heatmap mode colors decoded-to-source
surface distance on one shared scale across every method and frame.

Dense meshes are simplified component-by-component with Open3D for video
rendering. This preserves continuous surfaces and small disconnected parts;
triangles are never randomly removed from the displayed mesh.

```bash
../benchmark_app/.venv/bin/python prepare_assets.py
../benchmark_app/.venv/bin/streamlit run app.py --server.port 8502
```
