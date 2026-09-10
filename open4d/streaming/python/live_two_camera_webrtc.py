#!/usr/bin/env python3
"""Browser-accessible live viewer for the two-camera fusion receiver."""

from __future__ import annotations

import argparse
import json
import os
import queue
import signal
import statistics
import threading
import time
from collections import deque
from pathlib import Path

os.environ.setdefault("WEBRTC_IP", "127.0.0.1")

import numpy as np
import open3d as o3d

import live_two_camera_fusion as fusion


class BrowserFusion:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.transform = fusion.load_transform(args.j3_to_ey)
        self.ey = fusion.CameraProjector(args.ey_factory)
        self.j3 = fusion.CameraProjector(args.j3_factory)
        self.server = fusion.FrameServer(args)
        self.mesh_path = (
            args.output_dir / "latest_live_full_scene_mesh.ply"
        )
        self.mesh_worker = fusion.MeshWorker(
            self.ey,
            self.j3,
            self.transform,
            args.mesh_voxel,
            args.mesh_truncation,
            self.mesh_path,
            args.mesh_device,
            args.mesh_block_count,
            args.mesh_fusion_mode,
            args.mesh_merge_mode,
            args.mesh_weld_radius,
        )
        self.history: deque[fusion.PreparedPair] = deque(
            maxlen=args.mesh_window
        )
        self.latest_cloud = o3d.geometry.PointCloud()
        self.processed = 0
        self.mesh_updates = 0
        self.latest_pair = 0
        self.last_mesh_request = 0.0
        self.last_history_sample = 0.0
        self.processing_times: list[float] = []
        self.cloud_added = False
        self.mesh_added = False
        self.camera_initialized = False
        self.stop = False
        self.point_material = o3d.visualization.rendering.MaterialRecord()
        self.point_material.shader = "defaultUnlit"
        self.point_material.point_size = args.point_size
        self.mesh_material = o3d.visualization.rendering.MaterialRecord()
        self.mesh_material.shader = "defaultLit"
        self.processor: threading.Thread | None = None
        self.processor_error: str | None = None

    def request_stop(self, *_args) -> None:
        self.stop = True

    def on_init(self, visualizer: o3d.visualization.O3DVisualizer) -> None:
        visualizer.show_settings = True
        visualizer.set_background(
            np.asarray([0.025, 0.03, 0.045, 1.0], dtype=np.float32),
            None,
        )
        self.processor = threading.Thread(
            target=self._process_loop,
            args=(visualizer,),
            name="browser-fusion-processor",
        )
        self.processor.start()

    def save(self, _visualizer=None) -> None:
        self.args.output_dir.mkdir(parents=True, exist_ok=True)
        if len(self.latest_cloud.points):
            o3d.io.write_point_cloud(
                str(
                    self.args.output_dir
                    / "latest_live_full_scene_pointcloud.ply"
                ),
                self.latest_cloud,
                write_ascii=False,
                compressed=True,
            )
        print("Saved current full-scene browser-view outputs", flush=True)

    def _update_cloud(
        self,
        visualizer: o3d.visualization.O3DVisualizer,
        cloud: o3d.geometry.PointCloud,
    ) -> None:
        if self.cloud_added:
            visualizer.remove_geometry("Live fused point cloud")
        visualizer.add_geometry(
            "Live fused point cloud",
            cloud,
            self.point_material,
            "Live",
        )
        self.cloud_added = True
        show_cloud = self.args.display_mode in ("pointcloud", "both") or (
            self.args.display_mode == "auto" and not self.mesh_added
        )
        visualizer.show_geometry("Live fused point cloud", show_cloud)
        if not self.camera_initialized:
            visualizer.reset_camera_to_default()
            self.camera_initialized = True

    def _update_mesh(
        self,
        visualizer: o3d.visualization.O3DVisualizer,
        mesh: o3d.geometry.TriangleMesh,
    ) -> None:
        if self.mesh_added:
            visualizer.remove_geometry("Rolling full-scene TSDF mesh")
        visualizer.add_geometry(
            "Rolling full-scene TSDF mesh",
            mesh,
            self.mesh_material,
            "Mesh",
        )
        self.mesh_added = True
        show_mesh = self.args.display_mode in ("mesh", "both", "auto")
        visualizer.show_geometry("Rolling full-scene TSDF mesh", show_mesh)
        if self.args.display_mode == "auto" and self.cloud_added:
            visualizer.show_geometry("Live fused point cloud", False)

    def _post(self, visualizer, callback) -> None:
        o3d.visualization.gui.Application.instance.post_to_main_thread(
            visualizer, callback
        )

    def _process_loop(
        self, visualizer: o3d.visualization.O3DVisualizer
    ) -> None:
        try:
            while not self.stop:
                if self.server.fatal_error:
                    raise RuntimeError(self.server.fatal_error)
                if self.mesh_worker.error:
                    raise RuntimeError(self.mesh_worker.error)
                try:
                    decoded = self.server.frames.get(timeout=0.03)
                except queue.Empty:
                    decoded = None
                if decoded is not None:
                    started = time.perf_counter()
                    ey_bgr = self.ey.decode_color(decoded.ey_color_jpeg)
                    j3_bgr = self.j3.decode_color(decoded.j3_color_jpeg)
                    cloud = fusion.make_fused_cloud(
                        decoded,
                        ey_bgr,
                        j3_bgr,
                        self.ey,
                        self.j3,
                        self.transform,
                        self.args.point_stride,
                        self.args.point_voxel,
                    )
                    self.latest_cloud = cloud
                    self._post(
                        visualizer,
                        lambda cloud=cloud: self._update_cloud(
                            visualizer, cloud
                        ),
                    )
                    self.processed += 1
                    self.latest_pair = decoded.number
                    now = time.monotonic()
                    sample_period = max(
                        0.2,
                        self.args.mesh_interval / self.args.mesh_window,
                    )
                    if (
                        len(self.history) < self.args.mesh_window
                        or now - self.last_history_sample >= sample_period
                    ):
                        prepared = fusion.prepare_pair(
                            decoded,
                            self.ey,
                            self.j3,
                            ey_bgr,
                            j3_bgr,
                        )
                        self.history.append(prepared)
                        self.last_history_sample = now
                    self.processing_times.append(
                        time.perf_counter() - started
                    )
                    if (
                        len(self.history) >= self.args.mesh_window
                        and now - self.last_mesh_request
                        >= self.args.mesh_interval
                        and self.mesh_worker.request(list(self.history))
                    ):
                        self.last_mesh_request = now
                    if self.processed % self.args.log_every == 0:
                        print(
                            json.dumps(
                                {
                                    "processed_pairs": self.processed,
                                    "latest_pair": self.latest_pair,
                                    "live_points": len(cloud.points),
                                    "mean_processing_ms": (
                                        1000.0
                                        * statistics.mean(
                                            self.processing_times[
                                                -self.args.log_every :
                                            ]
                                        )
                                    ),
                                    "mesh_updates": self.mesh_updates,
                                }
                            ),
                            flush=True,
                        )
                try:
                    mesh_result = self.mesh_worker.results.get_nowait()
                except queue.Empty:
                    mesh_result = None
                if mesh_result is not None:
                    self._post(
                        visualizer,
                        lambda result=mesh_result: self._update_mesh(
                            visualizer, result.mesh
                        ),
                    )
                    self.mesh_updates += 1
                    print(
                        json.dumps(
                            {
                                "mesh_update": self.mesh_updates,
                                "source_pair": mesh_result.source_pair,
                                "vertices": len(mesh_result.mesh.vertices),
                                "triangles": len(mesh_result.mesh.triangles),
                                "build_seconds": mesh_result.build_seconds,
                                "backend": mesh_result.backend,
                                "fusion_mode": mesh_result.fusion_mode,
                                "merge_mode": mesh_result.merge_mode,
                                "merge_seconds": mesh_result.merge_seconds,
                                "partial_vertices": (
                                    mesh_result.partial_vertices
                                ),
                                "partial_triangles": (
                                    mesh_result.partial_triangles
                                ),
                                "saved": str(self.mesh_path),
                            }
                        ),
                        flush=True,
                    )
                if (
                    self.args.max_pairs
                    and self.server.received >= self.args.max_pairs
                    and self.server.frames.empty()
                ):
                    self.stop = True
            self._post(
                visualizer,
                o3d.visualization.gui.Application.instance.quit,
            )
        except Exception as error:
            self.processor_error = repr(error)
            print(f"Browser fusion processor error: {error}", flush=True)
            self._post(
                visualizer,
                o3d.visualization.gui.Application.instance.quit,
            )

    def run(self) -> None:
        self.args.output_dir.mkdir(parents=True, exist_ok=True)
        self.server.start()
        print(
            "Browser viewer ready through Ubuntu 127.0.0.1:8888",
            flush=True,
        )
        try:
            o3d.visualization.webrtc_server.enable_webrtc()
            o3d.visualization.draw(
                [],
                title="Two-Camera Femto Bolt Live Fusion",
                width=1440,
                height=900,
                actions=[("Save full-scene outputs", self.save)],
                bg_color=(0.025, 0.03, 0.045, 1.0),
                show_skybox=False,
                show_ui=True,
                point_size=self.args.point_size,
                on_init=self.on_init,
            )
        finally:
            self.stop = True
            if self.processor is not None:
                self.processor.join(timeout=10.0)
            self.save()
            self.server.close()
            self.mesh_worker.close()
            report = {
                "status": "completed",
                "viewer": "Open3D WebRTC",
                "full_scene": True,
                "spatial_crop": False,
                "component_filtering": False,
                "geometry_decimation": False,
                "processed_pairs": self.processed,
                "latest_pair": self.latest_pair,
                "mesh_updates": self.mesh_updates,
                "mesh_device": str(self.mesh_worker.device),
                "mesh_fusion_mode": self.mesh_worker.fusion_mode,
                "mesh_merge_mode": self.mesh_worker.merge_mode,
                "processor_error": self.processor_error,
                "receiver": self.server.report(),
            }
            (self.args.output_dir / "live_webrtc_report.json").write_text(
                json.dumps(report, indent=2), encoding="utf-8"
            )
            print(json.dumps(report, indent=2), flush=True)


def parse_args() -> argparse.Namespace:
    module_root = Path(__file__).resolve().parents[1]
    root = Path(os.environ.get("FOURD_CAPTURE_ROOT", module_root))
    calibration = Path(
        os.environ.get("FOURD_CALIBRATION_DIR", root / "calibration_2026-07-29")
    )
    factory = (
        calibration / "source/work/calibration_stepwise/factory"
    )
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17000)
    parser.add_argument("--allow-nonloopback", action="store_true")
    parser.add_argument("--socket-timeout", type=float, default=10.0)
    parser.add_argument("--delay-usec", type=int, default=160)
    parser.add_argument("--sync-tolerance-usec", type=int, default=3000)
    parser.add_argument("--max-pairs", type=int, default=0)
    parser.add_argument("--log-every", type=int, default=10)
    parser.add_argument(
        "--point-stride", type=int, choices=(1, 2, 3, 4), default=2
    )
    parser.add_argument("--point-voxel", type=float, default=0.004)
    parser.add_argument("--point-size", type=int, default=3)
    parser.add_argument(
        "--display-mode",
        choices=("auto", "pointcloud", "mesh", "both"),
        default="auto",
        help=(
            "auto shows the cloud until the first mesh, then hides it; "
            "both intentionally overlays the two products"
        ),
    )
    parser.add_argument("--mesh-window", type=int, default=7)
    parser.add_argument("--mesh-interval", type=float, default=4.0)
    parser.add_argument("--mesh-voxel", type=float, default=0.006)
    parser.add_argument("--mesh-truncation", type=float, default=0.03)
    parser.add_argument(
        "--mesh-device",
        default=os.environ.get("FOURD_MESH_DEVICE", "auto"),
        help="TSDF mesh device: auto, CPU:0, CUDA:0, CUDA:1, ...",
    )
    parser.add_argument("--mesh-block-count", type=int, default=20000)
    parser.add_argument(
        "--mesh-fusion-mode",
        choices=("shared-tsdf", "independent-merge"),
        default="independent-merge",
    )
    parser.add_argument(
        "--mesh-merge-mode",
        choices=("concatenate", "weld"),
        default="concatenate",
    )
    parser.add_argument("--mesh-weld-radius", type=float, default=0.003)
    parser.add_argument(
        "--ey-factory",
        type=Path,
        default=factory / "ey_factory_calibration.json",
    )
    parser.add_argument(
        "--j3-factory",
        type=Path,
        default=factory / "j3_factory_calibration.json",
    )
    parser.add_argument(
        "--j3-to-ey",
        type=Path,
        default=(
            calibration
            / "final_validated_fusion"
            / "j3_depth_to_ey_depth_refined.txt"
        ),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=module_root / "output" / "two-camera-fusion",
    )
    args = parser.parse_args()
    if not 1 <= args.mesh_window <= 31:
        parser.error("--mesh-window must be between 1 and 31")
    if args.mesh_block_count < 1000:
        parser.error("--mesh-block-count must be at least 1000")
    if args.mesh_weld_radius <= 0:
        parser.error("--mesh-weld-radius must be positive")
    return args


def main() -> None:
    args = parse_args()
    for path in (args.ey_factory, args.j3_factory, args.j3_to_ey):
        if not path.is_file():
            raise SystemExit(f"required calibration file missing: {path}")
    app = BrowserFusion(args)
    signal.signal(signal.SIGINT, app.request_stop)
    signal.signal(signal.SIGTERM, app.request_stop)
    app.run()


if __name__ == "__main__":
    main()
