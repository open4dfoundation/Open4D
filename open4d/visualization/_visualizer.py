"""Stateful visualization without importing a GUI backend at package import."""

from __future__ import annotations

from dataclasses import asdict
import math
import queue
import threading
import time

import numpy as np

from open4d.core import Frame, GaussianCloud, PointCloud, TriangleMesh

from ._api import _options
from ._frames import GaussianFrame, UP_TO_Z, gaussian_geometry, to_render_frame, texture_pixels


_UNCHANGED = object()


def _texture(image):
    """Validate an explicit top-to-bottom RGB(A) image; no hidden file loading."""
    if image is None:
        return None
    pixels = np.asarray(image)
    if pixels.ndim != 3 or pixels.shape[2] not in (3, 4) or min(pixels.shape[:2]) < 1:
        raise ValueError("texture must be a nonempty (height, width, 3 or 4) image")
    if pixels.dtype == np.uint8:
        return np.ascontiguousarray(pixels)
    if pixels.dtype.kind != "f" or not np.isfinite(pixels).all() or np.any((pixels < 0) | (pixels > 1)):
        raise ValueError("texture must contain uint8 pixels or finite floats in [0, 1]")
    return np.ascontiguousarray(np.rint(pixels * 255).astype(np.uint8))


class Visualizer:
    """Qt viewer with explicit window, geometry, event and rendering lifetimes.

    Constructing this object needs only NumPy. ``create_window`` loads the
    optional ``open4d[player]`` dependencies and must run on the main thread.
    Call ``poll_events`` regularly when feeding frames yourself, or use
    ``stream`` to keep Qt responsive while an iterator waits for network data.

    Geometry is an Open4D Frame, TriangleMesh, PointCloud, GaussianCloud,
    GaussianSplats or NeuralGaussianFrame. Gaussians are drawn as splats from
    their covariance, opacity and color; GaussianSplats SH is re-evaluated when
    the camera moves. A NeuralGaussianFrame's colors are evaluated once, for
    the camera when the frame is shown, since each evaluation runs the Vega
    worker. A context without instanced OpenGL falls back to Gaussian centers.
    Textures are explicit RGB(A) arrays using UVs with origin at bottom left.
    Per-vertex and per-corner UVs are interpolated by the GPU. Comparison panes
    share camera controls but do not infer correspondence or error metrics.
    Caller-owned frames, sequences and stream receivers are never closed.
    """

    def __init__(self, *, up: str = "z", **viewer_options):
        if up not in UP_TO_Z:
            raise ValueError(f"up must be one of {tuple(UP_TO_Z)}")
        self.up = up
        self.options = _options(30.0, viewer_options)
        self._backend = None
        self._geometries = {}

    @property
    def is_open(self) -> bool:
        return self._backend is not None and self._backend.is_open

    def create_window(self, *, title=None, width=None, height=None) -> bool:
        """Open a nonblocking window; an already-open window is an error."""
        if self.is_open:
            raise RuntimeError("a visualizer window is already open")
        if threading.current_thread() is not threading.main_thread():
            raise RuntimeError("Qt visualization must run on the main thread")
        values = asdict(self.options)
        fps = values.pop("fps")
        values.update({key: value for key, value in
                       {"title": title, "width": width, "height": height}.items()
                       if value is not None})
        options = _options(fps, values)
        from ._live_qt import LiveWindow

        self._backend = LiveWindow(options)
        self.options = options
        self._geometries.clear()
        return True

    def _require_window(self):
        if threading.current_thread() is not threading.main_thread():
            raise RuntimeError("Qt visualization must run on the main thread")
        if not self.is_open:
            raise RuntimeError("create_window must be called before rendering geometry")
        return self._backend

    def _prepare_geometry(self, geometry, texture):
        if gaussian_geometry(geometry):
            geometry = GaussianFrame(0, 0.0, geometry)
        if isinstance(geometry, GaussianFrame) and gaussian_geometry(geometry.geometry):
            if texture is not None:
                raise ValueError("textured geometry needs triangles and texture_coordinates")
            return to_render_frame(geometry, UP_TO_Z[self.up]), None
        if not isinstance(geometry, Frame):
            if not isinstance(geometry, (TriangleMesh, PointCloud, GaussianCloud)):
                raise TypeError("geometry must be a Frame, TriangleMesh, PointCloud, "
                                "GaussianCloud, GaussianSplats or NeuralGaussianFrame")
            geometry = Frame(0, 0.0, geometry)
        if not isinstance(geometry.geometry, (TriangleMesh, PointCloud, GaussianCloud)):
            raise TypeError("this viewer supports mesh, point and GaussianCloud frames")
        frame = to_render_frame(geometry, UP_TO_Z[self.up])
        pixels = texture_pixels(frame) if texture is None else _texture(texture)
        if pixels is not None and (not frame.is_mesh or frame.texture_coordinates is None):
            raise ValueError("textured geometry needs triangles and texture_coordinates")
        return frame, pixels

    @staticmethod
    def _key(name, baseline):
        if not isinstance(name, str) or not name:
            raise ValueError("name must be a nonempty string")
        if not isinstance(baseline, bool):
            raise TypeError("baseline must be a bool")
        return baseline, name

    def add_geometry(self, geometry, *, name="geometry", baseline=False,
                     texture=None, reset_camera=True) -> bool:
        """Add a named geometry to the current or baseline pane."""
        backend = self._require_window()
        key = self._key(name, baseline)
        if key in self._geometries:
            raise ValueError(f"geometry {name!r} already exists in this pane")
        frame, pixels = self._prepare_geometry(geometry, texture)
        backend.set_geometry(key, frame, pixels, reset_camera=reset_camera)
        self._geometries[key] = frame, pixels
        return True

    def update_geometry(self, geometry, *, name="geometry", baseline=False,
                        texture=_UNCHANGED, reset_camera=False) -> bool:
        """Replace a geometry; keep its texture unless explicitly changed."""
        backend = self._require_window()
        key = self._key(name, baseline)
        if key not in self._geometries:
            raise KeyError(f"no geometry {name!r} in this pane; call add_geometry first")
        if texture is _UNCHANGED:
            mesh = geometry.geometry if isinstance(geometry, Frame) else geometry
            texture = None if getattr(mesh, "texture", None) is not None else self._geometries[key][1]
        frame, pixels = self._prepare_geometry(geometry, texture)
        backend.set_geometry(key, frame, pixels, reset_camera=reset_camera)
        self._geometries[key] = frame, pixels
        return True

    def remove_geometry(self, *, name="geometry", baseline=False) -> bool:
        backend = self._require_window()
        key = self._key(name, baseline)
        if key not in self._geometries:
            return False
        backend.remove_geometry(key)
        del self._geometries[key]
        return True

    def set_options(self, **viewer_options) -> None:
        """Change point size, wireframe, surface color or other viewer controls."""
        backend = self._require_window()
        values = asdict(self.options)
        fps = values.pop("fps")
        values.update(viewer_options)
        options = _options(fps, values)
        backend.set_options(options)
        self.options = options

    def poll_events(self) -> bool:
        """Process pending events; return False after the user closes the window."""
        if threading.current_thread() is not threading.main_thread():
            raise RuntimeError("Qt visualization must run on the main thread")
        if self._backend is None:
            return False
        return self._backend.poll_events()

    def render(self) -> None:
        """Schedule rendering of the latest geometry in every visible pane."""
        self._require_window().render()

    update_renderer = render

    def capture_screen(self, *, baseline=False) -> np.ndarray:
        """Return an RGB uint8 framebuffer; requires a working desktop OpenGL context."""
        return self._require_window().capture(baseline=baseline)

    def run(self) -> None:
        """Run until the window closes, keeping explicitly added geometry visible."""
        self._require_window()
        while self.poll_events():
            self.render()
            time.sleep(0.005)

    def play(self, sequence, *, baseline=None, stride=1, fps=None,
             cache_size=3, texture=None, baseline_texture=None, up=None) -> None:
        """Loop equal-length sequences with matching frame timestamps.

        Sequences are open4d.Sequences or the Gaussian sources ``visualize``
        accepts. Use ``visualize`` for the one-line API. This method retains
        the same bounded lazy frame cache; it does not load an entire sequence
        into RAM.
        ``up`` defaults to this visualizer's coordinate convention.
        """
        from ._api import _prepare

        backend = self._require_window()
        if baseline is None and baseline_texture is not None:
            raise ValueError("baseline_texture requires a baseline sequence")
        axes = self.up if up is None else up
        frames, playback_fps = _prepare(sequence, stride=stride, fps=fps,
                                        up=axes, cache_size=cache_size)
        reference = None
        if baseline is not None:
            reference, _ = _prepare(baseline, stride=stride, fps=fps,
                                     up=axes, cache_size=cache_size)
            if len(reference) != len(frames):
                raise ValueError("baseline and current sequences must have equal frame counts")
        pixels = _texture(texture)
        reference_pixels = _texture(baseline_texture)
        ordinal, deadline = 0, 0.0
        while self.poll_events():
            now = time.monotonic()
            if now >= deadline:
                pair = [(False, frames[ordinal], pixels if pixels is not None else texture_pixels(frames[ordinal]))]
                if reference is not None:
                    other = reference[ordinal]
                    if not math.isclose(pair[0][1].timestamp, other.timestamp,
                                        rel_tol=1e-7, abs_tol=1e-6):
                        raise ValueError(f"baseline and current timestamps differ at frame {ordinal}")
                    pair.append((True, other, reference_pixels if reference_pixels is not None else texture_pixels(other)))
                # Validate both panes before changing either one.
                if any(image is not None and (not frame.is_mesh or frame.texture_coordinates is None)
                       for _, frame, image in pair):
                    raise ValueError("textured geometry needs triangles and texture_coordinates")
                for baseline_pane, frame, image in pair:
                    key = baseline_pane, "geometry"
                    backend.set_geometry(key, frame, image, reset_camera=key not in self._geometries)
                    self._geometries[key] = frame, image
                self.render()
                ordinal = (ordinal + 1) % len(frames)
                deadline = now + 1.0 / playback_fps
            time.sleep(0.005)

    def stream(self, frames, *, name="geometry", baseline=None,
               texture=None, baseline_texture=None) -> int:
        """Consume a Frame iterator until exhaustion or window close.

        One worker reads the iterator into a bounded queue; GUI operations stay
        on the main thread. Every consumed frame is displayed without accumulating
        the stream. A blocking iterator remains owned by the caller: use receiver
        timeouts and its context manager to release it when this call returns.
        Errors from the source iterator are re-raised in the caller.
        """
        self._require_window()
        if baseline is None and baseline_texture is not None:
            raise ValueError("baseline_texture requires baseline geometry")
        if baseline is not None:
            self.add_geometry(baseline, name=name, baseline=True, texture=baseline_texture)
        mailbox = queue.Queue(maxsize=1)
        stop = threading.Event()
        exhausted = object()

        def offer(value):
            while not stop.is_set():
                try:
                    mailbox.put(value, timeout=0.05)
                    return
                except queue.Full:
                    pass

        def read():
            try:
                for frame in frames:
                    if stop.is_set():
                        return
                    offer((frame, None))
            except Exception as error:
                offer((None, error))
            finally:
                offer((exhausted, None))

        worker = threading.Thread(target=read, name="open4d-viewer-source", daemon=True)
        worker.start()
        count = 0
        try:
            while self.poll_events():
                try:
                    frame, error = mailbox.get(timeout=0.005)
                except queue.Empty:
                    continue
                if error is not None:
                    raise error
                if frame is exhausted:
                    break
                if (False, name) in self._geometries:
                    self.update_geometry(frame, name=name, texture=texture)
                else:
                    self.add_geometry(frame, name=name, texture=texture)
                self.render()
                count += 1
        finally:
            stop.set()
            worker.join(timeout=0.1)
        return count

    def close(self) -> None:
        """Release this viewer's window and GPU resources; safe to call twice."""
        if self._backend is not None:
            if threading.current_thread() is not threading.main_thread():
                raise RuntimeError("Qt visualization must run on the main thread")
            self._backend.close()
            self._backend = None
        self._geometries.clear()

    destroy_window = close

    def __enter__(self):
        self.create_window()
        return self

    def __exit__(self, *exc):
        self.close()
