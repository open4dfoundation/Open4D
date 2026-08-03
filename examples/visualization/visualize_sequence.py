"""Load your 4D sequence, report what it contains, and play it with Open3D.

    python examples/visualization/visualize_sequence.py my_capture/
    python examples/visualization/visualize_sequence.py my_capture/ --info
    python examples/visualization/visualize_sequence.py my_capture/ --up y --save out.gif
    python examples/visualization/visualize_sequence.py my_capture.usdc

Point it at your own data. A source is either a folder holding one mesh file per
frame (`.obj`, `.ply`, `.usd`, or anything trimesh reads) or a single
time-sampled USD file. Meshes and point clouds are both handled: a frame with no
faces is drawn as a point cloud.

Start with `--info`. It reports frame count, duration, topology and bounds
without decoding geometry or opening a window, which is the quickest way to see
whether a dataset loads and what the loader made of it.

In the window: drag to orbit, scroll to zoom, and drag the time slider to scrub
through the sequence.

The window uses Open3D's Filament renderer — physically-based shading, a
directional sun, soft shadows. `--save` cannot: Filament has no headless backend
on macOS and capturing from its window drops frames, so saved GIFs come from
Open3D's older viewer and look flatter than the window does.
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path
from typing import Any

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import existing_source, require
from frame_sources import describe_source, open_sequence, supported_formats

# Open3D's default camera is +Y up looking down -Z, so the source's up axis has
# to land on Y — send it to Z and you get a view straight down onto the subject.
# Each entry is a cyclic rotation, so it reorients without mirroring.
UP_AXIS_ORDER = {"x": [2, 0, 1], "y": [0, 1, 2], "z": [1, 2, 0]}

# Name the single geometry slot the viewer swaps frames through.
GEOMETRY_NAME = "sequence"

# Index of the up axis after reordering.
PLOT_UP = 1


def report(sequence, path: Path) -> None:
    """Print what the sequence declares, before decoding any geometry."""
    print(f"\n{path}")
    print(f"  {describe_source(path)}")
    print(f"  frames     : {len(sequence)}")
    print(f"  duration   : {sequence.duration:.3f} s at "
          f"{sequence.fps or 0:.2f} fps")
    print(f"  topology   : {sequence.topology.value}")

    # A USD container records its own frame rate, up axis and key frames; a
    # frame folder has none of that, so only print what is actually there.
    for key in ("up_axis", "prim", "prim_type", "key_frame_indices", "format"):
        if key in sequence.metadata:
            value = sequence.metadata[key]
            if key == "key_frame_indices" and len(value) > 12:
                value = f"{list(value[:12])} ... ({len(value)} total)"
            print(f"  {key:<17}: {value}")


def resolve_up(sequence, requested: str | None) -> str:
    """Pick the up axis: the flag wins, then whatever the source recorded."""
    if requested:
        return requested
    recorded = str(sequence.metadata.get("up_axis", "")).lower()
    return recorded if recorded in UP_AXIS_ORDER else "z"


def to_open3d(frame, up: str, o3d: Any) -> Any:
    """Convert one core Frame into an Open3D mesh or point cloud.

    `integrations.open3d.frame_to_open3d` accepts a core `TriangleMesh`
    directly, and returns a `PointCloud` for a frame with no faces.
    """
    from integrations.open3d import frame_to_open3d

    mesh = frame.geometry
    order = UP_AXIS_ORDER[up]
    if order != UP_AXIS_ORDER["y"]:
        # Rebuild with reordered positions rather than mutating the frame.
        from open4d import TriangleMesh

        mesh = TriangleMesh(
            positions=np.ascontiguousarray(mesh.positions[:, order]),
            triangles=mesh.triangles,
            colors=mesh.colors,
        )

    geometry = frame_to_open3d(mesh)
    if isinstance(geometry, o3d.geometry.TriangleMesh):
        # Without normals Open3D shades the mesh flat grey.
        geometry.compute_vertex_normals()
    return geometry


def decode_all(sequence, stride: int, up: str, o3d: Any) -> list:
    """Decode every frame we intend to show, up front.

    Open3D playback has to keep up with the frame clock, so the frames are
    converted once here rather than parsed off disk inside the draw loop.
    """
    return [to_open3d(frame, up, o3d) for frame in sequence[::stride]]


def report_geometry(frames: list, stride: int, o3d: Any) -> None:
    """Print what the decoded frames turned out to be."""
    is_mesh = isinstance(frames[0], o3d.geometry.TriangleMesh)
    counts = [
        len(frame.vertices if is_mesh else frame.points) for frame in frames
    ]
    faces = [len(frame.triangles) if is_mesh else 0 for frame in frames]
    lower = np.min([frame.get_min_bound() for frame in frames], axis=0)
    upper = np.max([frame.get_max_bound() for frame in frames], axis=0)

    def summarize(values: list[int]) -> str:
        unique = sorted(set(values))
        if len(unique) == 1:
            return str(unique[0])
        return f"{unique[0]}..{unique[-1]} ({len(unique)} distinct)"

    print(f"\ndecoded {len(frames)} frames (stride {stride})")
    print(f"  geometry   : {'triangle mesh' if is_mesh else 'point cloud'}")
    print(f"  vertices   : {summarize(counts)}")
    if is_mesh:
        print(f"  triangles  : {summarize(faces)}")
    print(f"  bounds     : {lower.round(2)} .. {upper.round(2)}")

    # After reordering the up axis is PLOT_UP. A subject much longer along a
    # different axis usually means the source up axis was guessed wrong; a
    # genuinely wide flat subject is excluded by comparing against the second
    # longest rather than against up.
    extents = upper - lower
    longest = int(np.argmax(extents))
    if longest != PLOT_UP:
        runner_up = float(np.partition(extents, -2)[-2])
        if extents[longest] > 1.5 * max(runner_up, 1e-9):
            print("  note: the sequence is longest across the view, not "
                  "upright — the up axis may be wrong, try --up x/y/z")


SUN_DIRECTION = (-0.4, -1.0, -0.5)

SHADOW_PROFILES = {
    "none": "NO_SHADOWS",
    "hard": "HARD_SHADOWS",
    "medium": "MED_SHADOWS",
    "soft": "SOFT_SHADOWS",
    "dark": "DARK_SHADOWS",
}


def make_material(args, rendering: Any) -> Any:
    """A physically-based material, so the surface reads as a solid.

    `defaultLit` is Filament's PBR shader. Frames that carry their own vertex
    colors still show them; base_color tints an uncolored mesh.
    """
    material = rendering.MaterialRecord()
    material.shader = "defaultLit"
    material.base_color = [*args.color, 1.0]
    material.base_roughness = args.roughness
    material.base_metallic = 0.0
    material.point_size = args.point_size
    return material


def make_viewer(frames: list, args, o3d: Any):
    """Build the Filament-backed viewer with every frame time-tagged.

    Open3D's older `Visualizer` is a single camera-mounted light with no
    shadows, which renders a mesh as a flat silhouette. `O3DVisualizer` drives
    Filament: PBR materials, a directional sun, soft shadows, a ground plane,
    and a time slider.

    Each frame is added with its own `time`, which is what the animation system
    keys on — it then handles playback, looping and scrubbing itself. Do not try
    to drive frames with `show_geometry`: its own documentation notes that
    visibility is ignored while an animation is in progress, so every frame but
    the first renders empty.
    """
    from open3d.visualization import gui, rendering

    application = gui.Application.instance
    application.initialize()

    viewer = o3d.visualization.O3DVisualizer(
        "Open4D", args.width, args.height
    )
    # The time slider lives in the settings panel, so it is on by default.
    viewer.show_settings = not args.no_settings
    viewer.show_skybox(False)
    viewer.set_background([*args.background, 1.0], None)

    material = make_material(args, rendering)
    seconds_per_frame = 1.0 / args.fps
    for index, frame in enumerate(frames):
        viewer.add_geometry(
            f"{GEOMETRY_NAME}_{index:05d}",
            frame,
            material,
            time=index * seconds_per_frame,
        )

    viewer.scene.set_lighting(
        getattr(rendering.Open3DScene, SHADOW_PROFILES[args.shadows]),
        np.array(SUN_DIRECTION),
    )
    viewer.show_ground = not args.no_ground
    if args.wireframe:
        viewer.scene_shader = o3d.visualization.O3DVisualizer.UNLIT
    viewer.reset_camera_to_default()

    viewer.animation_time_step = seconds_per_frame
    viewer.animation_duration = len(frames) * seconds_per_frame
    application.add_window(viewer)
    return application, viewer


def play(frames: list, args, o3d: Any) -> None:
    """Play the sequence in a Filament window, on Open3D's own timeline."""
    application, viewer = make_viewer(frames, args, o3d)
    viewer.is_animating = True

    print(f"\nplaying {len(frames)} frames at {args.fps:g} fps — drag to orbit, "
          "scroll to zoom, use the time slider to scrub")
    application.run()


def record(frames: list, args, o3d: Any, output: Path) -> None:
    """Render every frame offscreen and write an animated GIF.

    This deliberately uses Open3D's older `Visualizer` rather than the Filament
    viewer `play()` uses. Filament has no headless backend on macOS, and driving
    a Filament window to capture frames proved unreliable —
    `export_current_image` silently fails to produce a file for a good fraction
    of frames. The old viewer captures every frame, at the cost of a single
    camera-mounted light: saved GIFs are flatter than the window.
    """
    image_module = require("PIL.Image", "open3d")
    if output.suffix.lower() != ".gif":
        raise SystemExit(
            f"--save writes an animated .gif; got {output.suffix or 'no suffix'}"
        )

    visualizer = o3d.visualization.Visualizer()
    visualizer.create_window(
        window_name="Open4D", width=args.width, height=args.height, visible=False
    )
    options = visualizer.get_render_option()
    options.point_size = args.point_size
    options.mesh_show_back_face = True
    options.mesh_show_wireframe = args.wireframe
    options.background_color = np.asarray(args.background)

    geometry = frames[0]
    visualizer.add_geometry(geometry, reset_bounding_box=True)
    control = visualizer.get_view_control()
    if args.yaw or args.pitch:
        # rotate() takes pixel-equivalent amounts, roughly 5 per degree.
        control.rotate(args.yaw, args.pitch)
    control.set_zoom(args.zoom)

    captured = []
    try:
        for index, frame in enumerate(frames):
            # Swapping the buffers inside the geometry the visualizer already
            # owns leaves the camera alone; add/remove would reframe it.
            if isinstance(geometry, o3d.geometry.TriangleMesh):
                geometry.vertices = frame.vertices
                geometry.triangles = frame.triangles
                geometry.vertex_colors = frame.vertex_colors
                geometry.vertex_normals = frame.vertex_normals
            else:
                geometry.points = frame.points
                geometry.colors = frame.colors
                geometry.normals = frame.normals
            visualizer.update_geometry(geometry)
            visualizer.poll_events()
            visualizer.update_renderer()
            buffer = np.asarray(
                visualizer.capture_screen_float_buffer(do_render=True)
            )
            captured.append(
                image_module.fromarray(
                    (255.0 * np.clip(buffer, 0.0, 1.0)).astype(np.uint8)
                )
            )
            print(f"\r  rendered {index + 1}/{len(frames)}", end="", flush=True)
    finally:
        visualizer.destroy_window()
    print()

    output.parent.mkdir(parents=True, exist_ok=True)
    captured[0].save(
        output,
        save_all=True,
        append_images=captured[1:],
        duration=max(int(1000.0 / args.fps), 20),
        loop=0,
        optimize=True,
    )
    print(f"wrote {output} ({output.stat().st_size / 1e6:.2f} MB)")


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=supported_formats(),
    )
    parser.add_argument(
        "path",
        type=Path,
        nargs="?",
        help="your sequence: a folder of per-frame mesh files, or a USD "
        "container",
    )
    parser.add_argument(
        "--stride", type=int, default=1, help="keep every Nth frame"
    )
    parser.add_argument(
        "--fps",
        type=float,
        default=30.0,
        help="frame rate assigned to a folder, and the playback rate",
    )
    parser.add_argument(
        "--up",
        choices=sorted(UP_AXIS_ORDER),
        default=None,
        help="which data axis points up; defaults to what a USD container "
        "records, otherwise z",
    )
    parser.add_argument(
        "--point-size", type=float, default=3.0, help="point-cloud marker size"
    )
    parser.add_argument("--width", type=int, default=960, help="window width")
    parser.add_argument("--height", type=int, default=960, help="window height")
    parser.add_argument(
        "--shadows",
        choices=sorted(SHADOW_PROFILES),
        default="soft",
        help="window: lighting and shadow strength (default soft)",
    )
    parser.add_argument(
        "--color",
        type=float,
        nargs=3,
        metavar=("R", "G", "B"),
        default=(0.78, 0.78, 0.80),
        help="surface colour in 0-1, for frames without their own",
    )
    parser.add_argument(
        "--roughness",
        type=float,
        default=0.5,
        help="window: 0 is mirror-like, 1 is fully matte",
    )
    parser.add_argument(
        "--background",
        type=float,
        nargs=3,
        metavar=("R", "G", "B"),
        default=(1.0, 1.0, 1.0),
        help="background colour in 0-1",
    )
    parser.add_argument(
        "--no-ground", action="store_true", help="window: hide the ground plane"
    )
    parser.add_argument(
        "--wireframe", action="store_true", help="draw unlit, showing structure"
    )
    parser.add_argument(
        "--no-settings",
        action="store_true",
        help="window: hide Open3D's settings panel, and with it the time slider",
    )
    parser.add_argument(
        "--zoom", type=float, default=0.6, help="--save camera: smaller is closer"
    )
    parser.add_argument(
        "--yaw",
        type=float,
        default=0.0,
        help="--save camera: turn around the subject, about 5 per degree",
    )
    parser.add_argument(
        "--pitch",
        type=float,
        default=0.0,
        help="--save camera: raise or lower",
    )
    parser.add_argument(
        "--save", type=Path, help="render offscreen to an animated .gif"
    )
    parser.add_argument(
        "--info", action="store_true", help="report the sequence and stop"
    )
    parser.add_argument(
        "--pack-usd",
        type=Path,
        help="also write the sequence to a .usdc OpenUSD container",
    )
    args = parser.parse_args()
    if args.path is None:
        # Full help rather than a one-line usage error: the epilog lists every
        # format, which is what someone pointing this at new data needs to see.
        parser.print_help()
        raise SystemExit(2)
    if args.stride < 1:
        parser.error("--stride must be at least 1")
    if args.fps <= 0:
        parser.error("--fps must be greater than zero")

    path = existing_source(args.path)

    # A malformed frame in someone else's dataset is ordinary, not a crash, so
    # report it as an error naming the file rather than a traceback.
    try:
        with open_sequence(path, fps=args.fps) as sequence:
            report(sequence, path)
            if not len(sequence):
                raise SystemExit(f"{path} contains no frames")

            if args.pack_usd:
                from formats_usd import write_usd_container

                # A folder reports its formats as a list, a container as one
                # string; record either as a plain string.
                source_format = sequence.metadata.get("format", "")
                if isinstance(source_format, (list, tuple)):
                    source_format = ", ".join(source_format)

                # Prefer the rate the provider declares. Sequence.fps derives
                # it from the timestamps, which lands on 30.000000000000004
                # rather than 30 and writes that into the container.
                declared = sequence.metadata.get("fps")
                fps = float(declared) if declared else (sequence.fps or args.fps)

                written = write_usd_container(
                    args.pack_usd,
                    sequence,
                    fps=fps,
                    up_axis=resolve_up(sequence, args.up),
                    source=str(path),
                    source_format=str(source_format),
                    generator="examples/visualization/visualize_sequence.py",
                )
                print(f"\nwrote {written} "
                      f"({written.stat().st_size / 1e6:.2f} MB)")

            if args.info:
                return

            o3d = require("open3d", "open3d")
            up = resolve_up(sequence, args.up)
            frames = decode_all(sequence, args.stride, up, o3d)
    except (ValueError, TypeError, OSError) as error:
        raise SystemExit(f"\nfailed to read the sequence:\n  {error}") from None

    report_geometry(frames, args.stride, o3d)
    if args.save:
        record(frames, args, o3d, args.save)
    else:
        play(frames, args, o3d)


if __name__ == "__main__":
    main()
