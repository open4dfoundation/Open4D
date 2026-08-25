"""A PyQt6 + pyqtgraph viewer for 4D sequences.

Built on PyQt6 and pyqtgraph, which unlike Open3D install on current Python —
Open3D publishes no wheels for 3.13.

Playback is a GL view with a transport bar underneath: play/pause, a frame
slider, and a readout. Mouse drag orbits, scroll zooms. Space toggles playback,
left/right step a frame, `r` restarts, `q` closes.

Shading is baked into vertex colors by `render_frames.shade` rather than left to
pyqtgraph's `shaded` shader, which lights from the camera and so flattens the
surface as the view moves.

The scene and the window are built separately. `--save` wants a GL widget of an
exact size and no chrome; a widget that has never been shown reports its default
640x480 and renders at that aspect, so sizing it explicitly matters.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np

from ._deps import require
from . import _frames as render_frames


def metrics_lines(frame, position: int, total: int, fps: float) -> list[str]:
    """The few numbers worth showing while a sequence plays.

    `frame.frame_index` is the source's own numbering, which differs from the
    position in the list whenever --stride drops frames.
    """
    lines = [
        f"frame {position + 1}/{total}   (source #{frame.frame_index})",
        f"t {frame.timestamp:.3f}s   @{fps:g} fps",
        f"{len(frame.positions):,} verts",
    ]
    if frame.is_mesh:
        lines.append(f"{len(frame.triangles):,} tris")
    else:
        lines.append("point cloud")
    return lines


def _qt():
    """Import PyQt6 and pyqtgraph's GL module, or exit with the pip command."""
    QtWidgets = require("PyQt6.QtWidgets", "player")
    QtCore = require("PyQt6.QtCore", "player")
    gl = require("pyqtgraph.opengl", "player")
    return QtWidgets, QtCore, gl


def check_available(*, gif: bool = False) -> None:
    """Fail before geometry decoding when the requested backend is unavailable."""
    _qt()
    if gif:
        require("PIL.Image", "player")


class Scene:
    """A GL view holding one sequence, with the frame it shows swappable."""

    def __init__(self, frames: list, args) -> None:
        QtWidgets, _QtCore, gl = _qt()
        from pyqtgraph import Vector
        from pyqtgraph.opengl import shaders

        # PyQtGraph caches program IDs globally, but they belong to the GL
        # context destroyed with the previous viewer window.
        shaders.initShaders()

        # Qt refuses to build widgets before an application exists.
        self.application = (
            QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        )
        self.frames = frames
        self.args = args
        self.index = 0
        self.gl = gl

        self.view = gl.GLViewWidget()
        self.view.setBackgroundColor(
            tuple(int(255 * channel) for channel in args.background)
        )

        lower, upper = render_frames.bounds(frames)
        center = (lower + upper) / 2.0
        span = float(np.max(upper - lower)) or 1.0
        # pyqtgraph keeps the orbit centre in opts and measures distance in world
        # units, so both have to be scaled to the subject, not the origin.
        self.view.opts["center"] = Vector(*(float(value) for value in center))
        self.view.setCameraPosition(
            distance=span * args.distance,
            elevation=args.elevation,
            azimuth=args.azimuth,
        )

        # Both item kinds exist from the start and visibility is switched per
        # frame, so a folder mixing meshes and point clouds still draws.
        first = frames[0]
        self.mesh_item = gl.GLMeshItem(
            meshdata=self._mesh_data(first) if first.is_mesh else gl.MeshData(),
            smooth=False,
            drawFaces=True,
            drawEdges=args.wireframe,
            edgeColor=(0.2, 0.2, 0.25, 1.0),
        )
        self.point_item = gl.GLScatterPlotItem(
            pos=first.positions,
            color=self._colors(first),
            size=args.point_size,
            pxMode=True,
        )
        # pyqtgraph gives a scatter additive blending by default, which *adds*
        # colour to whatever is behind it. On a white background that saturates
        # to white and the points vanish entirely — correct-looking code drawing
        # nothing. Normal alpha blending works against any background.
        self.point_item.setGLOptions("translucent")
        self.view.addItem(self.mesh_item)
        self.view.addItem(self.point_item)
        self.show_frame(0)

    def _colors(self, frame) -> np.ndarray:
        """Per-vertex RGBA, honouring the requested surface colour."""
        return render_frames.vertex_colors(
            frame, base=tuple(self.args.color), ambient=self.args.ambient
        )

    def _mesh_data(self, frame):
        return self.gl.MeshData(
            vertexes=frame.positions,
            faces=frame.triangles.astype(np.int32),
            vertexColors=self._colors(frame),
        )

    def show_frame(self, index: int):
        """Display a frame, wrapping the index, and return it."""
        self.index = index % len(self.frames)
        frame = self.frames[self.index]
        if frame.is_mesh:
            self.mesh_item.setMeshData(meshdata=self._mesh_data(frame))
        else:
            self.point_item.setData(
                pos=frame.positions, color=self._colors(frame)
            )
        self.mesh_item.setVisible(frame.is_mesh)
        self.point_item.setVisible(not frame.is_mesh)
        return frame

    def grab(self, image_module):
        """Render the current frame and return it as a PIL image.

        The framebuffer comes back at the display's device pixel ratio, so it is
        scaled back down to the size that was asked for.
        """
        self.view.update()
        self.application.processEvents()
        image = self.view.grabFramebuffer()
        if image.isNull():
            raise RuntimeError(
                "Qt could not create an OpenGL framebuffer. Use a desktop "
                "session with OpenGL support; QT_QPA_PLATFORM=offscreen does "
                "not support this viewer."
            )
        image = image.convertToFormat(image.Format.Format_RGB888)
        width, height = image.width(), image.height()
        raw = image.constBits().asstring(height * image.bytesPerLine())
        # Rows are padded to a stride; crop the padding before reshaping.
        pixels = np.frombuffer(raw, dtype=np.uint8).reshape(
            height, image.bytesPerLine() // 3, 3
        )[:, :width]
        picture = image_module.fromarray(pixels.copy())
        target = (self.args.width, self.args.height)
        if picture.size != target:
            picture = picture.resize(target, image_module.LANCZOS)
        if not self.args.no_metrics:
            self._draw_metrics(picture)
        return picture

    def _draw_metrics(self, picture) -> None:
        """Burn the metrics into a captured frame.

        `grabFramebuffer` returns only the GL surface, so the window's overlay
        label is not in it — the text has to be drawn again here for a saved GIF
        to show the same numbers.
        """
        from PIL import ImageDraw, ImageFont

        lines = metrics_lines(
            self.frames[self.index], self.index, len(self.frames), self.args.fps
        )
        draw = ImageDraw.Draw(picture)
        try:
            font = ImageFont.truetype("Helvetica", 13)
        except OSError:
            # Pillow's built-in bitmap font is always available.
            font = ImageFont.load_default()
        for row, line in enumerate(lines):
            draw.text((12, 10 + 16 * row), line, fill=(70, 70, 78), font=font)


def play(frames: list, args) -> None:
    """Open the window and run until it is closed."""
    QtWidgets, QtCore, _gl = _qt()
    scene = Scene(frames, args)

    class Window(QtWidgets.QMainWindow):
        def __init__(self) -> None:
            super().__init__()
            self.setWindowTitle(args.title)
            self.resize(args.width, args.height)
            if args.x is not None and args.y is not None:
                self.move(args.x, args.y)
            self.playing = True

            self.play_button = QtWidgets.QPushButton("Pause")
            self.play_button.setFixedWidth(80)
            self.play_button.clicked.connect(self.toggle)

            self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
            self.slider.setRange(0, len(frames) - 1)
            self.slider.valueChanged.connect(self.scrub)

            # Just the position, since the corner overlay carries the detail.
            self.readout = QtWidgets.QLabel()
            self.readout.setFixedWidth(70)
            self.readout.setAlignment(
                QtCore.Qt.AlignmentFlag.AlignRight
                | QtCore.Qt.AlignmentFlag.AlignVCenter
            )

            # A child of the GL widget, so it sits in the view's top-left corner
            # and stays there when the window is resized.
            self.metrics = QtWidgets.QLabel(scene.view)
            self.metrics.setVisible(not args.no_metrics)
            self.metrics.setStyleSheet(
                "color: rgb(70,70,78);"
                "background: rgba(255,255,255,170);"
                "padding: 6px 8px; border-radius: 4px;"
                "font-family: Menlo, Consolas, monospace; font-size: 11px;"
            )
            self.metrics.move(10, 10)

            transport = QtWidgets.QHBoxLayout()
            transport.addWidget(self.play_button)
            transport.addWidget(self.slider)
            transport.addWidget(self.readout)

            layout = QtWidgets.QVBoxLayout()
            layout.setContentsMargins(8, 8, 8, 8)
            layout.addWidget(scene.view, stretch=1)
            layout.addLayout(transport)
            central = QtWidgets.QWidget()
            central.setLayout(layout)
            self.setCentralWidget(central)

            self.timer = QtCore.QTimer(self)
            self.timer.timeout.connect(self.advance)
            self.timer.start(max(int(1000.0 / args.fps), 1))
            self.refresh(0)

        def refresh(self, index: int) -> None:
            frame = scene.show_frame(index)
            self.readout.setText(f"{scene.index + 1}/{len(frames)}")
            self.metrics.setText(
                "\n".join(
                    metrics_lines(frame, scene.index, len(frames), args.fps)
                )
            )
            self.metrics.adjustSize()
            if self.slider.value() != scene.index:
                self.slider.blockSignals(True)
                self.slider.setValue(scene.index)
                self.slider.blockSignals(False)

        def advance(self) -> None:
            if self.playing:
                self.refresh(scene.index + 1)

        def toggle(self) -> None:
            self.playing = not self.playing
            self.play_button.setText("Pause" if self.playing else "Play")

        def scrub(self, value: int) -> None:
            self.playing = False
            self.play_button.setText("Play")
            self.refresh(value)

        def keyPressEvent(self, event) -> None:
            key = event.key()
            if key == QtCore.Qt.Key.Key_Space:
                self.toggle()
            elif key == QtCore.Qt.Key.Key_Right:
                self.scrub((scene.index + 1) % len(frames))
            elif key == QtCore.Qt.Key.Key_Left:
                self.scrub((scene.index - 1) % len(frames))
            elif key == QtCore.Qt.Key.Key_R:
                self.playing = True
                self.play_button.setText("Pause")
                self.refresh(0)
            elif key in (QtCore.Qt.Key.Key_Q, QtCore.Qt.Key.Key_Escape):
                self.close()
            else:
                super().keyPressEvent(event)

    window = Window()
    print(f"\nplaying {len(frames)} frames at {args.fps:g} fps — drag to orbit, "
          "scroll to zoom, space pauses, left/right step, q quits")
    window.show()
    scene.application.exec()


def record(frames: list, args, output: Path) -> None:
    """Render every frame to an animated GIF, without opening a window."""
    image_module = require("PIL.Image", "player")
    if output.suffix.lower() != ".gif":
        raise ValueError(
            f"--save writes an animated .gif; got {output.suffix or 'no suffix'}"
        )

    scene = Scene(frames, args)
    # An unshown widget keeps its default 640x480 and renders at that aspect, so
    # size it explicitly rather than having to show the window.
    scene.view.resize(args.width, args.height)
    scene.application.processEvents()

    captured = []
    for index in range(len(frames)):
        scene.show_frame(index)
        captured.append(scene.grab(image_module))
        print(f"\r  rendered {index + 1}/{len(frames)}", end="", flush=True)
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
