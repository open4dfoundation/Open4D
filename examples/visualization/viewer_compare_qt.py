"""A PyQt6 + pyqtgraph viewer showing two sequences side by side.

Every pane shares one camera. Orbiting or zooming any of them drives the others,
because the whole point is to look at the same feature in both at once — two
independently posed views of a compressed mesh tell you nothing.

    viewer_compare_qt.play(comparison, args)
    viewer_compare_qt.record(comparison, args, Path("compare.gif"))

Panes are described by a `(which, mode)` pair: which sequence they draw, and
whether they draw it shaded or coloured by error. The error scale is fixed for
the whole sequence and shown as a colourbar with numeric ticks, so a colour in
frame 1 means what it means in frame 100 and nobody has to read magnitude out of
hue alone.

Like the single-sequence viewer, the window and `--save` share one renderer, so
a saved GIF matches what was on screen — including the colourbar, which is drawn
from the same lookup table in both.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np

# Import first: this puts the repository on sys.path for uninstalled clones.
from _common import require
import colormaps
import compare_frames
from open4d.visualization import _frames as render_frames

# Ink colours for overlay text, on the dark surface the comparison defaults to.
_TEXT = (222, 222, 228)
_TEXT_DIM = (150, 150, 158)


@dataclass(frozen=True)
class PaneSpec:
    """What one pane draws."""

    which: str   # "reference" or "decoded"
    mode: str    # "shaded" or "error"


# The two panes: the reference as geometry, the decoded mesh as its error field.
PANES = (PaneSpec("reference", "shaded"), PaneSpec("decoded", "error"))


def _qt():
    """Import PyQt6 and pyqtgraph's GL module, or exit with the pip command.

    Also enables OpenGL context sharing, which more than one GL view *requires*
    here: pyqtgraph caches each compiled shader program globally while every
    `GLViewWidget` gets its own context, so the second pane would look the
    program up in a context that has never seen it and fail to draw with
    GL_INVALID_VALUE. Qt only honours the attribute before the application is
    constructed, so it is set here rather than beside the panes.
    """
    QtWidgets = require("PyQt6.QtWidgets", "player")
    QtCore = require("PyQt6.QtCore", "player")
    QtGui = require("PyQt6.QtGui", "player")
    gl = require("pyqtgraph.opengl", "player")
    if QtWidgets.QApplication.instance() is None:
        QtCore.QCoreApplication.setAttribute(
            QtCore.Qt.ApplicationAttribute.AA_ShareOpenGLContexts, True
        )
    return QtWidgets, QtCore, QtGui, gl


def metrics_lines(comparison, index: int, fps: float) -> list[str]:
    """The numbers worth showing while a comparison plays."""
    frame = comparison.frames[index]
    error = frame.error
    forward = error.forward
    name = "point-to-point" if error.metric == "point" else "point-to-plane"

    lines = [
        f"frame {index + 1}/{len(comparison)}   (source #{frame.decoded.frame_index})",
        f"t {frame.decoded.timestamp:.3f}s   @{fps:g} fps",
        f"ref  {len(frame.reference.positions):,} v  {len(frame.reference.triangles):,} f",
        f"dec  {len(frame.decoded.positions):,} v  {len(frame.decoded.triangles):,} f",
        "",
        f"{name}, decoded → reference",
        f"RMS  {forward.rms:.6g}",
        f"max  {forward.maximum:.6g}",
        f"PSNR {forward.psnr_db:.2f} dB",
        f"symmetric RMS {error.symmetric_rms:.6g}",
    ]
    return lines


def colorbar_ticks(clamp: float, percentile: float | None) -> list[tuple[float, str]]:
    """Tick positions in 0..1 and their labels.

    The top tick is marked as a clamp whenever values above it were folded into
    the last colour, which is exactly when the scale came from a percentile.
    """
    if clamp <= 0.0:
        return [(0.0, "0"), (1.0, "0  (exact match)")]
    ticks = [(fraction, f"{fraction * clamp:.4g}") for fraction in (0.0, 0.25, 0.5, 0.75)]
    top = f"≥ {clamp:.4g}" if percentile is not None else f"{clamp:.4g}"
    ticks.append((1.0, top))
    return ticks


class _Pane:
    """One GL view drawing one side of the comparison."""

    def __init__(self, spec: PaneSpec, comparison, args, gl_module) -> None:
        self.spec = spec
        self.comparison = comparison
        self.args = args
        self.gl = gl_module
        self.mode = spec.mode

        self.view = _linked_view(gl_module)()
        self.view.setBackgroundColor(
            tuple(int(255 * channel) for channel in args.background)
        )
        self._frame_camera()

        first = comparison.frames[0].frame_for(spec.which)

        self.mesh_item = gl_module.GLMeshItem(
            meshdata=gl_module.MeshData(),
            smooth=False,
            drawFaces=True,
            drawEdges=args.wireframe,
            edgeColor=(0.35, 0.35, 0.4, 1.0),
        )
        self.point_item = gl_module.GLScatterPlotItem(
            pos=first.positions, size=args.point_size, pxMode=True
        )
        # Additive blending is pyqtgraph's scatter default and washes out against
        # any light surface; the single-sequence viewer needs the same fix.
        self.point_item.setGLOptions("translucent")
        self.view.addItem(self.mesh_item)
        self.view.addItem(self.point_item)

    @property
    def title(self) -> str:
        if self.mode == "error":
            return "error: decoded → reference"
        return self.spec.which

    def _frame_camera(self) -> None:
        """Point the camera at both sequences' shared bounds.

        Framing each pane on its own subject would defeat the shared camera: a
        decoded mesh with a slightly different bounding box would sit at a
        different apparent scale.
        """
        from pyqtgraph import Vector

        every = [
            frame.frame_for(side)
            for frame in self.comparison.frames
            for side in ("reference", "decoded")
        ]
        lower, upper = render_frames.bounds(every)
        center = (lower + upper) / 2.0
        span = float(np.max(upper - lower)) or 1.0
        self.view.opts["center"] = Vector(*(float(value) for value in center))
        self.view.setCameraPosition(
            distance=span * self.args.distance,
            elevation=self.args.elevation,
            azimuth=self.args.azimuth,
        )

    def colors(self, index: int) -> np.ndarray:
        frame = self.comparison.frames[index]
        if self.mode == "error":
            return compare_frames.error_vertex_colors(
                frame,
                "decoded",
                self.comparison.clamp,
                shading=self.args.error_shading,
            )
        return render_frames.vertex_colors(
            frame.frame_for(self.spec.which),
            base=tuple(self.args.color),
            ambient=self.args.ambient,
        )

    def show_frame(self, index: int) -> None:
        frame = self.comparison.frames[index].frame_for(self.spec.which)
        colors = self.colors(index)
        if frame.is_mesh:
            self.mesh_item.setMeshData(
                meshdata=self.gl.MeshData(
                    vertexes=frame.positions,
                    faces=frame.triangles.astype(np.int32),
                    vertexColors=colors,
                )
            )
        else:
            self.point_item.setData(pos=frame.positions, color=colors)
        self.mesh_item.setVisible(frame.is_mesh)
        self.point_item.setVisible(not frame.is_mesh)


_LINKED_VIEW_CACHE: dict[int, type] = {}


def _linked_view(gl_module):
    """Build (once) a `GLViewWidget` subclass that mirrors its camera to peers.

    pyqtgraph offers no camera-changed signal, so the mouse handlers are wrapped.
    The class is created lazily because importing pyqtgraph at module scope would
    make `--info` require a GL stack it never uses.
    """
    key = id(gl_module)
    if key in _LINKED_VIEW_CACHE:
        return _LINKED_VIEW_CACHE[key]

    class LinkedGLView(gl_module.GLViewWidget):
        def __init__(self, *args, **kwargs) -> None:
            super().__init__(*args, **kwargs)
            self.peers: list = []
            self._receiving = False

        def _broadcast(self) -> None:
            if self._receiving:
                return
            from pyqtgraph import Vector

            center = self.opts["center"]
            for peer in self.peers:
                peer._receiving = True
                try:
                    # A fresh Vector per peer: pyqtgraph pans by mutating this in
                    # place in some versions, which would couple the panes twice
                    # over and drift them apart.
                    peer.opts["center"] = Vector(
                        center.x(), center.y(), center.z()
                    )
                    for key_name in ("distance", "elevation", "azimuth", "fov"):
                        peer.opts[key_name] = self.opts[key_name]
                    peer.update()
                finally:
                    peer._receiving = False

        def mouseMoveEvent(self, event) -> None:
            super().mouseMoveEvent(event)
            self._broadcast()

        def wheelEvent(self, event) -> None:
            super().wheelEvent(event)
            self._broadcast()

    _LINKED_VIEW_CACHE[key] = LinkedGLView
    return LinkedGLView


class Comparison3D:
    """The panes, their shared camera, and the frame they show."""

    def __init__(self, comparison, args) -> None:
        QtWidgets, _QtCore, _QtGui, gl = _qt()
        self.application = (
            QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        )
        self.comparison = comparison
        self.args = args
        self.index = 0
        self.panes = [_Pane(spec, comparison, args, gl) for spec in PANES]
        for pane in self.panes:
            pane.view.peers = [other.view for other in self.panes if other is not pane]
        self.show_frame(0)

    def show_frame(self, index: int) -> int:
        self.index = index % len(self.comparison)
        for pane in self.panes:
            pane.show_frame(self.index)
        return self.index

    def grab(self, image_module):
        """Render every pane and composite them into one PIL image."""
        images = []
        for pane in self.panes:
            pane.view.update()
            self.application.processEvents()
            images.append(_framebuffer(pane.view, self.args, image_module))

        gap = 8
        width = sum(image.width for image in images) + gap * (len(images) - 1)
        height = max(image.height for image in images)
        sheet = image_module.new(
            "RGB", (width, height + 62), _rgb255(self.args.background)
        )
        offset = 0
        for image in images:
            sheet.paste(image, (offset, 0))
            offset += image.width + gap
        _draw_overlay(sheet, self, images, height)
        return sheet


def _rgb255(color) -> tuple[int, int, int]:
    return tuple(int(255 * channel) for channel in color)


def _framebuffer(view, args, image_module):
    """Read one GL view back as a PIL image at the requested pane size."""
    image = view.grabFramebuffer()
    image = image.convertToFormat(image.Format.Format_RGB888)
    width, height = image.width(), image.height()
    raw = image.constBits().asstring(height * image.bytesPerLine())
    # Rows are padded to a stride; crop the padding before reshaping.
    pixels = np.frombuffer(raw, dtype=np.uint8).reshape(
        height, image.bytesPerLine() // 3, 3
    )[:, :width]
    picture = image_module.fromarray(pixels.copy())
    target = (args.width, args.height)
    if picture.size != target:
        picture = picture.resize(target, image_module.LANCZOS)
    return picture


def _font(size: int):
    from PIL import ImageFont

    try:
        return ImageFont.truetype("Menlo", size)
    except OSError:
        try:
            return ImageFont.truetype("DejaVuSansMono", size)
        except OSError:
            # Pillow's built-in bitmap font is always available.
            return ImageFont.load_default()


def _draw_overlay(sheet, scene, images, pane_height: int) -> None:
    """Burn titles, metrics and the colourbar into a captured composite.

    `grabFramebuffer` returns only the GL surface, so none of the window's Qt
    labels are in it; a saved GIF has to have them drawn again here.
    """
    from PIL import ImageDraw

    draw = ImageDraw.Draw(sheet)
    title_font, body_font = _font(13), _font(11)

    offset = 0
    for pane, image in zip(scene.panes, images):
        draw.text(
            (offset + 12, 10), pane.title, fill=_TEXT, font=title_font
        )
        offset += image.width + 8

    if not scene.args.no_metrics:
        lines = metrics_lines(scene.comparison, scene.index, scene.args.fps)
        # The window's overlay label has a translucent panel behind it; without
        # one here the text runs over the subject and stops being readable.
        draw = _darken_panel(sheet, draw, lines, body_font)
        for row, line in enumerate(lines):
            draw.text(
                (12, 32 + 14 * row), line, fill=_TEXT_DIM, font=body_font
            )

    _draw_colorbar(sheet, draw, scene, pane_height, body_font)


def _darken_panel(sheet, draw, lines: list[str], font):
    """Dim the area the metrics will be drawn over, and return a fresh drawer.

    Blending the region rather than pasting a flat rectangle keeps this correct
    whatever `--background` is set to, and darkening is enough: the overlay ink
    is light.
    """
    from PIL import Image

    width = max(
        (draw.textlength(line, font=font) for line in lines if line), default=0.0
    )
    box = (4, 26, int(min(width + 20, sheet.width)), 32 + 14 * len(lines))
    region = sheet.crop(box)
    sheet.paste(
        Image.blend(region, Image.new("RGB", region.size, (0, 0, 0)), 0.5), box[:2]
    )
    from PIL import ImageDraw

    return ImageDraw.Draw(sheet)


def _draw_colorbar(sheet, draw, scene, pane_height: int, font) -> None:
    """Draw the legend strip beneath the panes."""
    from PIL import Image

    comparison = scene.comparison
    left, right = 12, sheet.width - 12
    top = pane_height + 16
    bar_height = 12
    strip = colormaps.colorbar_strip(max(right - left, 1), bar_height)
    sheet.paste(Image.fromarray(strip), (left, top))

    label = (
        "point-to-point" if comparison.metric == "point" else "point-to-plane"
    )
    draw.text(
        (left, top - 15), f"{label} distance", fill=_TEXT_DIM, font=font
    )
    for fraction, text in colorbar_ticks(comparison.clamp, comparison.percentile):
        x = left + int(fraction * (right - left - 1))
        anchor = "la" if fraction == 0.0 else ("ra" if fraction == 1.0 else "ma")
        draw.text(
            (x, top + bar_height + 3), text, fill=_TEXT_DIM, font=font, anchor=anchor
        )


class _ColorBar:
    """Factory for the Qt colourbar widget, built lazily like the linked view."""

    _cache: dict[int, type] = {}

    @classmethod
    def make(cls, QtWidgets, QtCore, QtGui):
        key = id(QtWidgets)
        if key in cls._cache:
            return cls._cache[key]

        class ColorBarWidget(QtWidgets.QWidget):
            def __init__(self, comparison) -> None:
                super().__init__()
                self.comparison = comparison
                self.setFixedHeight(38)
                strip = colormaps.colorbar_strip(512, 1)
                self._gradient = QtGui.QImage(
                    strip.tobytes(), strip.shape[1], 1,
                    strip.shape[1] * 3, QtGui.QImage.Format.Format_RGB888,
                ).copy()

            def paintEvent(self, _event) -> None:
                painter = QtGui.QPainter(self)
                bar = QtCore.QRect(0, 2, self.width(), 12)
                painter.drawImage(bar, self._gradient)
                painter.setPen(QtGui.QColor(*_TEXT_DIM))
                painter.setFont(QtGui.QFont("Menlo", 8))
                for fraction, text in colorbar_ticks(
                    self.comparison.clamp, self.comparison.percentile
                ):
                    x = int(fraction * (self.width() - 1))
                    flag = QtCore.Qt.AlignmentFlag
                    align = flag.AlignHCenter
                    box = QtCore.QRect(x - 60, 16, 120, 18)
                    if fraction == 0.0:
                        align, box = flag.AlignLeft, QtCore.QRect(0, 16, 120, 18)
                    elif fraction == 1.0:
                        align = flag.AlignRight
                        box = QtCore.QRect(self.width() - 120, 16, 120, 18)
                    painter.drawText(box, align, text)
                painter.end()

        cls._cache[key] = ColorBarWidget
        return ColorBarWidget


def play(comparison, args) -> None:
    """Open the comparison window and run until it is closed."""
    QtWidgets, QtCore, QtGui, _gl = _qt()
    scene = Comparison3D(comparison, args)
    ColorBarWidget = _ColorBar.make(QtWidgets, QtCore, QtGui)

    class Window(QtWidgets.QMainWindow):
        def __init__(self) -> None:
            super().__init__()
            self.setWindowTitle("Open4D — compare")
            self.resize(
                args.width * len(scene.panes) + 40, args.height + 150
            )
            self.playing = True

            self.play_button = QtWidgets.QPushButton("Pause")
            self.play_button.setFixedWidth(80)
            self.play_button.clicked.connect(self.toggle)

            self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
            self.slider.setRange(0, len(comparison) - 1)
            self.slider.valueChanged.connect(self.scrub)

            self.readout = QtWidgets.QLabel()
            self.readout.setFixedWidth(70)
            self.readout.setAlignment(
                QtCore.Qt.AlignmentFlag.AlignRight
                | QtCore.Qt.AlignmentFlag.AlignVCenter
            )

            views = QtWidgets.QHBoxLayout()
            views.setSpacing(8)
            self.titles = []
            for pane in scene.panes:
                column = QtWidgets.QVBoxLayout()
                title = QtWidgets.QLabel(pane.title)
                title.setStyleSheet(
                    f"color: rgb{_TEXT}; font-family: Menlo, Consolas, monospace;"
                    "font-size: 12px; padding: 2px;"
                )
                column.addWidget(title)
                column.addWidget(pane.view, stretch=1)
                views.addLayout(column, stretch=1)
                self.titles.append(title)

            # Parented to the first pane's view so it tracks that corner on
            # resize, matching the single-sequence viewer's overlay.
            self.metrics = QtWidgets.QLabel(scene.panes[0].view)
            self.metrics.setVisible(not args.no_metrics)
            self.metrics.setStyleSheet(
                f"color: rgb{_TEXT};"
                "background: rgba(0,0,0,120);"
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
            layout.addLayout(views, stretch=1)
            layout.addWidget(ColorBarWidget(comparison))
            layout.addLayout(transport)
            central = QtWidgets.QWidget()
            central.setLayout(layout)
            self.setCentralWidget(central)

            self.timer = QtCore.QTimer(self)
            self.timer.timeout.connect(self.advance)
            self.timer.start(max(int(1000.0 / args.fps), 1))
            self.refresh(0)

        def refresh(self, index: int) -> None:
            position = scene.show_frame(index)
            self.readout.setText(f"{position + 1}/{len(comparison)}")
            self.metrics.setText(
                "\n".join(
                    metrics_lines(comparison, position, args.fps)
                )
            )
            self.metrics.adjustSize()
            for title, pane in zip(self.titles, scene.panes):
                title.setText(pane.title)
            if self.slider.value() != position:
                self.slider.blockSignals(True)
                self.slider.setValue(position)
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
            keys = QtCore.Qt.Key
            if key == keys.Key_Space:
                self.toggle()
            elif key == keys.Key_Right:
                self.scrub((scene.index + 1) % len(comparison))
            elif key == keys.Key_Left:
                self.scrub((scene.index - 1) % len(comparison))
            elif key == keys.Key_R:
                self.playing = True
                self.play_button.setText("Pause")
                self.refresh(0)
            elif key in (keys.Key_Q, keys.Key_Escape):
                self.close()
            else:
                super().keyPressEvent(event)

    window = Window()
    print(
        f"\ncomparing {len(comparison)} frames at {args.fps:g} fps — drag to orbit "
        "(both panes follow), scroll to zoom, space pauses, left/right step, "
        "q quits"
    )
    window.show()
    scene.application.exec()


def record(comparison, args, output: Path) -> None:
    """Render every frame to an animated GIF, without opening a window."""
    image_module = require("PIL.Image", "player")
    if output.suffix.lower() != ".gif":
        raise SystemExit(
            f"--save writes an animated .gif; got {output.suffix or 'no suffix'}"
        )

    scene = Comparison3D(comparison, args)
    # An unshown widget keeps its default 640x480 and renders at that aspect, so
    # size every pane explicitly rather than having to show the window.
    for pane in scene.panes:
        pane.view.resize(args.width, args.height)
    scene.application.processEvents()

    captured = []
    for index in range(len(comparison)):
        scene.show_frame(index)
        captured.append(scene.grab(image_module))
        print(f"\r  rendered {index + 1}/{len(comparison)}", end="", flush=True)
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
