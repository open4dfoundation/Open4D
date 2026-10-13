"""Optional Qt/OpenGL implementation for the explicit-lifetime Visualizer."""

from __future__ import annotations

import numpy as np

from ._qt import _application, _mesh_shader, _qt
from ._frames import bounds, vertex_colors


def _textured_item(gl):
    """Mesh item using existing color buffers to carry interpolated UVs."""
    from OpenGL import GL
    from pyqtgraph.opengl import shaders

    class TexturedMesh(gl.GLMeshItem):
        def __init__(self, frame, pixels, options):
            shader = shaders.ShaderProgram("open4d-texture", [
                shaders.VertexShader("""
                    uniform mat4 u_mvp;
                    attribute vec4 a_position;
                    attribute vec4 a_color;
                    varying vec2 v_uv;
                    void main() {
                        gl_Position = u_mvp * a_position;
                        v_uv = a_color.xy;
                    }
                """),
                shaders.FragmentShader("""
                    #ifdef GL_ES
                    precision mediump float;
                    #endif
                    uniform sampler2D u_texture;
                    varying vec2 v_uv;
                    void main() { gl_FragColor = texture2D(u_texture, v_uv); }
                """),
            ])
            super().__init__(shader=shader, smooth=False, computeNormals=False,
                             drawEdges=options.wireframe, glOptions="translucent")
            self.texture_id = None
            self.set_texture_data(frame, pixels)

        def set_texture_data(self, frame, pixels):
            # Expand corners to preserve UV seams instead of averaging them.
            uv = frame.texture_coordinates
            corners = uv[frame.triangles] if uv.ndim == 2 else uv
            vertexes = frame.positions[frame.triangles].reshape(-1, 3)
            colors = np.ones((len(vertexes), 4), dtype=np.float32)
            colors[:, :2] = corners.reshape(-1, 2)
            mesh = gl.MeshData(vertexes=vertexes,
                               faces=np.arange(len(vertexes), dtype=np.uint32).reshape(-1, 3),
                               vertexColors=colors)
            self.setMeshData(meshdata=mesh)
            # Explicit image updates may mutate an existing array in place.
            # Upload on the next paint even when its Python identity is stable.
            self.pixels = pixels
            self.texture_dirty = True

        def paint(self):
            old_active = int(GL.glGetIntegerv(GL.GL_ACTIVE_TEXTURE))
            GL.glActiveTexture(GL.GL_TEXTURE0)
            old_texture = int(GL.glGetIntegerv(GL.GL_TEXTURE_BINDING_2D))
            old_alignment = int(GL.glGetIntegerv(GL.GL_UNPACK_ALIGNMENT))
            try:
                if self.texture_id is None:
                    self.texture_id = int(GL.glGenTextures(1))
                GL.glBindTexture(GL.GL_TEXTURE_2D, self.texture_id)
                if self.texture_dirty:
                    pixels = np.ascontiguousarray(self.pixels[::-1])
                    format_ = GL.GL_RGB if pixels.shape[2] == 3 else GL.GL_RGBA
                    GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, 1)
                    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, format_, pixels.shape[1],
                                    pixels.shape[0], 0, format_, GL.GL_UNSIGNED_BYTE, pixels)
                    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
                    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
                    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_S, GL.GL_REPEAT)
                    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_WRAP_T, GL.GL_REPEAT)
                    self.texture_dirty = False
                # The sampler defaults to unit zero. ShaderProgram sets the MVP.
                super().paint()
            finally:
                GL.glPixelStorei(GL.GL_UNPACK_ALIGNMENT, old_alignment)
                GL.glBindTexture(GL.GL_TEXTURE_2D, old_texture)
                GL.glActiveTexture(old_active)

        def dispose(self):
            if self.texture_id is not None:
                GL.glDeleteTextures([self.texture_id])
                self.texture_id = None

    return TexturedMesh


class LiveWindow:
    """Nonblocking window with linked current/baseline cameras and real GL items."""

    def __init__(self, options):
        QtWidgets, QtCore, self.gl = _qt()
        self.application = _application(QtWidgets)
        self.options = options
        self.is_open = True
        self.items = {}
        self._syncing = False
        owner = self

        class CameraView(self.gl.GLViewWidget):
            def mouseMoveEvent(self, event):
                super().mouseMoveEvent(event)
                owner._sync_camera(self)

            def wheelEvent(self, event):
                super().wheelEvent(event)
                owner._sync_camera(self)

        class Window(QtWidgets.QMainWindow):
            def closeEvent(self, event):
                owner._dispose()
                owner.is_open = False
                super().closeEvent(event)

        self.window = Window()
        self.window.setAttribute(QtCore.Qt.WidgetAttribute.WA_DeleteOnClose, True)
        self.window.setWindowTitle(options.title)
        self.window.resize(options.width, options.height)
        if options.x is not None:
            self.window.move(options.x, options.y)
        central = QtWidgets.QWidget()
        layout = QtWidgets.QHBoxLayout(central)
        self.panes, self.views, self.labels = {}, {}, {}
        for baseline, heading in ((True, "Baseline"), (False, "Current")):
            pane = QtWidgets.QWidget()
            column = QtWidgets.QVBoxLayout(pane)
            label = QtWidgets.QLabel(heading)
            view = CameraView()
            view.setBackgroundColor(tuple(int(255 * c) for c in options.background))
            view.setCameraPosition(distance=options.distance, elevation=options.elevation,
                                   azimuth=options.azimuth)
            column.addWidget(label)
            column.addWidget(view, stretch=1)
            layout.addWidget(pane, stretch=1)
            self.panes[baseline], self.views[baseline], self.labels[baseline] = pane, view, label
        self.panes[True].hide()
        self.window.setCentralWidget(central)
        self.window.show()
        self.application.processEvents()

    def _sync_camera(self, source):
        if self._syncing:
            return
        self._syncing = True
        try:
            for view in self.views.values():
                if view is not source:
                    for key in ("center", "distance", "elevation", "azimuth", "rotation", "fov"):
                        if key in source.opts:
                            view.opts[key] = source.opts[key]
                    view.update()
        finally:
            self._syncing = False

    def _make_item(self, frame, texture):
        if texture is not None:
            return _textured_item(self.gl)(frame, texture, self.options)
        if frame.is_mesh:
            return self.gl.GLMeshItem(meshdata=self._mesh_data(frame), shader=_mesh_shader(),
                                      smooth=False, drawEdges=self.options.wireframe)
        item = self.gl.GLScatterPlotItem(pos=frame.positions, color=self._colors(frame),
                                         size=self.options.point_size, pxMode=True)
        item.setGLOptions("translucent")
        return item

    def _colors(self, frame):
        return vertex_colors(frame, base=self.options.color, ambient=self.options.ambient)

    def _mesh_data(self, frame):
        return self.gl.MeshData(vertexes=frame.positions, faces=frame.triangles.astype(np.int32),
                                vertexColors=self._colors(frame))

    def set_geometry(self, key, frame, texture, *, reset_camera):
        previous = self.items.get(key)
        kind = "texture" if texture is not None else "mesh" if frame.is_mesh else "points"
        if previous is not None and previous[3] == kind:
            item = previous[0]
            if kind == "texture":
                item.set_texture_data(frame, texture)
            elif kind == "mesh":
                item.setMeshData(meshdata=self._mesh_data(frame))
            else:
                item.setData(pos=frame.positions, color=self._colors(frame), size=self.options.point_size)
        else:
            if previous is not None:
                self._remove_item(key)
            item = self._make_item(frame, texture)
            self.views[key[0]].addItem(item)
        self.items[key] = item, frame, texture, kind
        self.panes[True].setVisible(any(k[0] for k in self.items))
        self._refresh_labels()
        if reset_camera:
            self.reset_camera()
        self.render()

    def reset_camera(self):
        from pyqtgraph import Vector

        frames = [record[1] for record in self.items.values() if len(record[1].positions)]
        if not frames:
            return
        lower, upper = bounds(frames)
        center = (lower + upper) / 2
        span = float(np.max(upper - lower)) or 1.0
        for view in self.views.values():
            view.opts["center"] = Vector(*map(float, center))
            view.setCameraPosition(distance=span * self.options.distance,
                                   elevation=self.options.elevation, azimuth=self.options.azimuth)

    def _refresh_labels(self):
        for baseline, label in self.labels.items():
            heading = "Baseline" if baseline else "Current"
            frames = [record[1] for key, record in self.items.items() if key[0] == baseline]
            details = []
            if frames and not self.options.no_metrics:
                details.append(f"frame #{frames[-1].frame_index}  t {frames[-1].timestamp:.3f}s")
            if any(frame.representation == "gaussians" for frame in frames):
                details.append("Gaussian centers: point preview, not splat rendering")
            label.setText("  |  ".join([heading, *details]))

    def _remove_item(self, key):
        item = self.items[key][0]
        view = self.views[key[0]]
        if hasattr(item, "dispose"):
            view.makeCurrent()
            try:
                item.dispose()
            finally:
                view.doneCurrent()
        view.removeItem(item)
        del self.items[key]

    def remove_geometry(self, key):
        self._remove_item(key)
        self.panes[True].setVisible(any(k[0] for k in self.items))
        self._refresh_labels()

    def set_options(self, options):
        self.options = options
        self.window.setWindowTitle(options.title)
        self.window.resize(options.width, options.height)
        if options.x is not None:
            self.window.move(options.x, options.y)
        for view in self.views.values():
            view.setBackgroundColor(tuple(int(255 * c) for c in options.background))
        for key, (item, frame, texture, kind) in list(self.items.items()):
            if kind in ("texture", "mesh"):
                item.opts["drawEdges"] = options.wireframe
            # Rebuild mesh color/edge buffers and update point size immediately.
            self.set_geometry(key, frame, texture, reset_camera=False)
        self.reset_camera()
        self._refresh_labels()

    def poll_events(self):
        self.application.processEvents()
        return self.is_open

    def render(self):
        for view in self.views.values():
            view.update()

    def capture(self, *, baseline=False):
        self.render()
        self.application.processEvents()
        image = self.views[baseline].grabFramebuffer()
        if image.isNull():
            raise RuntimeError("Qt could not create an OpenGL framebuffer; use a desktop display")
        image = image.convertToFormat(image.Format.Format_RGB888)
        raw = image.constBits().asstring(image.height() * image.bytesPerLine())
        rows = np.frombuffer(raw, dtype=np.uint8).reshape(image.height(), image.bytesPerLine())
        return rows[:, :image.width() * 3].reshape(image.height(), image.width(), 3).copy()

    def _dispose(self):
        for key in list(self.items):
            self._remove_item(key)

    def close(self):
        if self.is_open:
            self.window.close()
            self.application.processEvents()
