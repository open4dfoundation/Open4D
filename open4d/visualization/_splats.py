"""Gaussian splat rendering for the Qt viewers.

This is the EWA splatting 3DGS uses, drawn the way the browser `SplatRenderer`
draws it: one instanced quad per Gaussian, its 3D covariance projected to a 2D
screen-space ellipse through the perspective Jacobian, widened by the 0.3 px
low-pass filter, and shaded with a Gaussian falloff out to 3 sigma. Quads are
blended back to front with premultiplied alpha.

The work is split so most of it is testable without a display. `SplatBuffer`
is the CPU half -- packed instance rows, the depth order and view-dependent
color for one camera -- and needs only NumPy. `splat_item` builds the
pyqtgraph item that uploads those rows and runs the shader, imported only once
a Qt viewer exists.

Sorting and SH evaluation run on the CPU, whenever the camera matrix changes.
That keeps the shader within GLSL 1.20 (macOS's legacy context) and keeps one
code path for every driver, at the cost of an argsort and one SH pass per
camera move. Contexts without instancing get pyqtgraph's point scatter instead.
"""

from __future__ import annotations

import ctypes
import functools

import numpy as np

from ._frames import SH_C0

# Graphdeco's real SH constants (`utils/sh_utils.py`), Condon-Shortley phase.
SH_C1 = 0.4886025119029199
SH_C2 = (1.0925484305920792, -1.0925484305920792, 0.31539156525252005,
         -1.0925484305920792, 0.5462742152960396)
SH_C3 = (-0.5900435899266435, 2.890611442640554, -0.4570457994644658,
         0.3731763325901154, -0.4570457994644658, 1.445305721320277,
         -0.5900435899266435)

# Floats per instance: center (3), covariance (6), premultiplied-later RGBA (4).
STRIDE = 13


def evaluate_sh(coefficients: np.ndarray, directions: np.ndarray) -> np.ndarray:
    """Sum (N, K, 3) SH of degree <= 3 for unit (N, 3) directions.

    The raw sum, as Graphdeco's `eval_sh` returns it; displayed RGB adds 0.5.
    Bands beyond the coefficient count are skipped, so K selects the degree.
    """
    sh = np.asarray(coefficients, dtype=np.float32)
    count = sh.shape[1]
    if count not in (1, 4, 9, 16):
        raise ValueError("SH coefficients must have 1, 4, 9 or 16 bands")
    result = SH_C0 * sh[:, 0]
    if count == 1:
        return result
    x, y, z = (np.asarray(directions, dtype=np.float32).T[:, :, None])
    result = result - SH_C1 * y * sh[:, 1] + SH_C1 * z * sh[:, 2] - SH_C1 * x * sh[:, 3]
    if count == 4:
        return result
    xx, yy, zz = x * x, y * y, z * z
    xy, yz, xz = x * y, y * z, x * z
    result = (result + SH_C2[0] * xy * sh[:, 4] + SH_C2[1] * yz * sh[:, 5]
              + SH_C2[2] * (2 * zz - xx - yy) * sh[:, 6]
              + SH_C2[3] * xz * sh[:, 7] + SH_C2[4] * (xx - yy) * sh[:, 8])
    if count == 9:
        return result
    return (result + SH_C3[0] * y * (3 * xx - yy) * sh[:, 9]
            + SH_C3[1] * xy * z * sh[:, 10]
            + SH_C3[2] * y * (4 * zz - xx - yy) * sh[:, 11]
            + SH_C3[3] * z * (2 * zz - 3 * xx - 3 * yy) * sh[:, 12]
            + SH_C3[4] * x * (4 * zz - xx - yy) * sh[:, 13]
            + SH_C3[5] * z * (xx - yy) * sh[:, 14]
            + SH_C3[6] * x * (xx - 3 * yy) * sh[:, 15])


def camera_eye(modelview: np.ndarray) -> np.ndarray:
    """World position of the camera for a row-major (4, 4) model-view matrix."""
    return np.linalg.inv(np.asarray(modelview, dtype=np.float64))[:3, 3]


def back_to_front(positions: np.ndarray, modelview: np.ndarray) -> np.ndarray:
    """Indices ordering splats farthest first for ``modelview`` (row-major).

    GL view space looks down -Z, so the most negative view depth is farthest.
    """
    matrix = np.asarray(modelview, dtype=np.float32)
    depth = positions @ matrix[2, :3] + matrix[2, 3]
    return np.argsort(depth, kind="stable")


def view_directions(positions: np.ndarray, eye: np.ndarray,
                    source_axes: np.ndarray | None) -> np.ndarray:
    """Unit camera-to-splat directions, rotated back into source axes."""
    directions = positions.astype(np.float32) - np.asarray(eye, dtype=np.float32)
    lengths = np.linalg.norm(directions, axis=1, keepdims=True)
    np.divide(directions, lengths, out=directions, where=lengths > 0)
    directions[lengths[:, 0] == 0] = (0.0, 0.0, 1.0)
    if source_axes is not None:
        # Row-vector form of Aᵀ d: plot = A source, so source = Aᵀ plot.
        directions = directions @ np.asarray(source_axes, dtype=np.float32)
    return directions


class SplatBuffer:
    """Instance rows for one Gaussian frame, sorted and colored per camera."""

    def __init__(self) -> None:
        self.frame = None
        self.packed = np.empty((0, STRIDE), dtype=np.float32)
        self._camera = None

    def __len__(self) -> int:
        return len(self.packed)

    def set_frame(self, frame, colors: np.ndarray) -> None:
        """Pack a Gaussian `RenderFrame` with its (N, 3) view-independent RGB."""
        packed = np.empty((len(frame.positions), STRIDE), dtype=np.float32)
        packed[:, 0:3] = frame.positions
        packed[:, 3:9] = frame.covariances
        packed[:, 9:12] = colors
        packed[:, 12] = (frame.opacities if frame.opacities is not None else 1.0)
        self.frame = frame
        self.packed = packed
        self._camera = None

    def instances(self, modelview: np.ndarray) -> np.ndarray | None:
        """Rows sorted back to front for this camera, or None if unchanged."""
        modelview = np.asarray(modelview, dtype=np.float32)
        key = modelview.tobytes()
        if key == self._camera:
            return None
        self._camera = key
        if not len(self.packed):
            return self.packed
        positions = self.packed[:, 0:3]
        order = back_to_front(positions, modelview)
        rows = self.packed[order]
        sh = self.frame.spherical_harmonics
        if sh is not None:
            directions = view_directions(positions, camera_eye(modelview),
                                         self.frame.source_axes)
            # Graphdeco clamps at zero only; the display also caps at one.
            rgb = np.clip(evaluate_sh(sh, directions) + 0.5, 0.0, 1.0)
            rows[:, 9:12] = rgb[order]
        return rows


class NeuralColors:
    """Small per-viewer cache of colors from a neural appearance model.

    `NeuralAppearance.colors` runs a Vega worker process, far too slow to call
    per camera move, so each frame is evaluated once -- for the camera at the
    time it is first shown -- and those colors are reused until it is evicted.
    """

    def __init__(self, size: int = 4) -> None:
        self.size = size
        self._entries: list[tuple[object, np.ndarray]] = []

    def __call__(self, frame, eye: np.ndarray) -> np.ndarray:
        appearance = frame.appearance
        for index, (key, colors) in enumerate(self._entries):
            if key is appearance or key == appearance:
                self._entries.append(self._entries.pop(index))
                return colors
        directions = view_directions(frame.positions, eye, frame.source_axes)
        colors = np.asarray(appearance.colors(directions), dtype=np.float32)
        if colors.shape != (len(frame.positions), 3) or not np.isfinite(colors).all():
            raise RuntimeError("the appearance model returned invalid colors")
        colors = np.clip(colors, 0.0, 1.0)
        self._entries.append((appearance, colors))
        del self._entries[:-self.size]
        return colors


def base_colors(frame, base, eye, neural: NeuralColors) -> np.ndarray:
    """(N, 3) RGB a Gaussian frame is drawn with before any SH term.

    Stored colors (GaussianCloud, or the SH DC term) win; a neural model is
    evaluated through ``neural``; colorless Gaussians take the surface color.
    """
    if frame.appearance is not None:
        return neural(frame, eye)
    if frame.colors is not None:
        return frame.colors
    return np.broadcast_to(np.asarray(base, dtype=np.float32), (len(frame.positions), 3))


def status(supported, reason) -> str:
    """How a viewer labels Gaussians, once it knows how they were drawn."""
    if supported is False:
        return f"Gaussian centers (point fallback: {reason})"
    return "Gaussian splats"


_VERTEX = """
uniform mat4 u_modelview;
uniform mat4 u_projection;
uniform vec2 u_viewport;        // device pixels

ATTRIBUTE vec2 a_corner;        // quad corner in sigma units, [-3, 3]
ATTRIBUTE vec3 a_center;
ATTRIBUTE vec3 a_cov0;          // xx, xy, xz
ATTRIBUTE vec3 a_cov1;          // yy, yz, zz
ATTRIBUTE vec4 a_color;         // RGB and opacity

VARYING vec2 v_corner;
VARYING vec4 v_color;

void main() {
    vec4 view = u_modelview * vec4(a_center, 1.0);
    vec4 clip = u_projection * view;
    // Behind the camera, or so close that the Jacobian blows up.
    if (clip.w <= 1e-6 || -view.z <= 1e-6) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    mat3 cov = mat3(a_cov0.x, a_cov0.y, a_cov0.z,
                    a_cov0.y, a_cov1.x, a_cov1.y,
                    a_cov0.z, a_cov1.y, a_cov1.z);
    mat3 rotation = mat3(u_modelview);
    mat3 covView = rotation * cov * transpose(rotation);

    // Pixel focal lengths from the projection; off-centre terms cancel in J.
    vec2 focal = vec2(u_projection[0][0], u_projection[1][1]) * 0.5 * u_viewport;
    float depth = -view.z;
    // 3DGS clamps the centre to 1.3x the frustum before linearising, so a
    // splat far off screen cannot stretch across the viewport.
    vec2 limit = 1.3 / vec2(u_projection[0][0], u_projection[1][1]);
    vec2 tangent = clamp(view.xy / depth, -limit, limit);
    // Columns of the 2x3 Jacobian of pixel position w.r.t. view position.
    mat3 jacobian = mat3(focal.x / depth, 0.0, 0.0,
                         0.0, focal.y / depth, 0.0,
                         focal.x * tangent.x / depth, focal.y * tangent.y / depth, 0.0);
    mat3 projected = jacobian * covView * transpose(jacobian);

    // The 0.3 px dilation is 3DGS's low-pass filter: without it a Gaussian
    // thinner than a pixel aliases into flicker as the camera moves.
    float a = projected[0][0] + 0.3;
    float b = projected[0][1];
    float c = projected[1][1] + 0.3;
    float mid = 0.5 * (a + c);
    float radius = sqrt(max(mid * mid - (a * c - b * b), 0.0));
    float major = mid + radius;
    float minor = mid - radius;
    if (minor <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    // Eigenvector for the major eigenvalue: (a - major) x + b y = 0.
    vec2 axis = abs(b) < 1e-12 ? (a >= c ? vec2(1.0, 0.0) : vec2(0.0, 1.0))
                               : normalize(vec2(b, major - a));
    vec2 majorAxis = min(sqrt(major), 2048.0) * axis;
    vec2 minorAxis = min(sqrt(minor), 2048.0) * vec2(-axis.y, axis.x);
    vec2 offset = a_corner.x * majorAxis + a_corner.y * minorAxis;
    gl_Position = clip + vec4(offset * 2.0 / u_viewport * clip.w, 0.0, 0.0);
    v_corner = a_corner;
    v_color = a_color;
}
"""

_FRAGMENT = """
#ifdef GL_ES
precision highp float;
#endif
VARYING vec2 v_corner;
VARYING vec4 v_color;
OUTPUT_DECLARATION

void main() {
    float squared = dot(v_corner, v_corner);
    if (squared > 9.0) discard;                    // outside 3 sigma
    float alpha = min(0.99, v_color.a * exp(-0.5 * squared));
    if (alpha < 1.0 / 255.0) discard;
    FRAGMENT_COLOR = vec4(v_color.rgb * alpha, alpha);   // premultiplied
}
"""

_ATTRIBUTES = ("a_corner", "a_center", "a_cov0", "a_cov1", "a_color")

# Triangle strip over a 3-sigma square.
_CORNERS = np.array([-3, -3, 3, -3, -3, 3, 3, 3], dtype=np.float32)


class SplatsUnsupported(RuntimeError):
    """The current OpenGL context cannot draw instanced splats."""


def capabilities(context):
    """GLSL sources and instancing entry points for ``context``, or raise.

    Desktop OpenGL 3.3 and OpenGL ES 3.0 have instancing in core. Older desktop
    contexts -- macOS's legacy 2.1 among them -- qualify through the ARB
    instanced-arrays and draw-instanced extensions with GLSL 1.20.
    """
    from OpenGL import GL
    from OpenGL.GL.ARB import draw_instanced, instanced_arrays

    version = context.format().version()
    if context.isOpenGLES():
        if version < (3, 0):
            raise SplatsUnsupported(f"OpenGL ES {version[0]}.{version[1]} has no instancing")
        header, modern = "#version 300 es\n", True
        divisor, draw = GL.glVertexAttribDivisor, GL.glDrawArraysInstanced
    else:
        modern = version >= (3, 1)
        header = "#version 140\n" if modern else "#version 120\n"
        if version >= (3, 3):
            divisor = GL.glVertexAttribDivisor
        elif context.hasExtension(b"GL_ARB_instanced_arrays"):
            divisor = instanced_arrays.glVertexAttribDivisorARB
        else:
            raise SplatsUnsupported("OpenGL lacks GL_ARB_instanced_arrays")
        if modern:
            draw = GL.glDrawArraysInstanced
        elif context.hasExtension(b"GL_ARB_draw_instanced"):
            draw = draw_instanced.glDrawArraysInstancedARB
        else:
            raise SplatsUnsupported("OpenGL lacks GL_ARB_draw_instanced")
    if not (bool(divisor) and bool(draw)):
        raise SplatsUnsupported("PyOpenGL could not load the instancing entry points")
    if modern:
        words = {"ATTRIBUTE": "in", "VARYING_V": "out", "VARYING_F": "in",
                 "OUTPUT_DECLARATION": "out vec4 fragColor;", "FRAGMENT_COLOR": "fragColor"}
    else:
        words = {"ATTRIBUTE": "attribute", "VARYING_V": "varying", "VARYING_F": "varying",
                 "OUTPUT_DECLARATION": "", "FRAGMENT_COLOR": "gl_FragColor"}
    vertex = _VERTEX.replace("ATTRIBUTE", words["ATTRIBUTE"]).replace("VARYING", words["VARYING_V"])
    fragment = _FRAGMENT.replace("VARYING", words["VARYING_F"])
    fragment = fragment.replace("OUTPUT_DECLARATION", words["OUTPUT_DECLARATION"])
    fragment = fragment.replace("FRAGMENT_COLOR", words["FRAGMENT_COLOR"])
    return header + vertex, header + fragment, divisor, draw


@functools.cache
def splat_item(gl):
    """The pyqtgraph item class; ``gl`` is the imported `pyqtgraph.opengl`."""
    from OpenGL import GL
    from OpenGL.GL import shaders
    from PyQt6 import QtGui, QtOpenGL

    class Scatter(gl.GLScatterPlotItem):
        """pyqtgraph's scatter with a program per item, not per class.

        The stock item caches one program on the class, so a second viewer
        whose context shares nothing with the first would use a stale name.
        """

        program = None

        def getShaderProgram(self):
            if self.program is None:
                stock = gl.GLScatterPlotItem
                cached, stock._shaderProgram = stock._shaderProgram, None
                try:
                    self.program = stock.getShaderProgram()
                finally:
                    stock._shaderProgram = cached
            return self.program

    class GLSplatItem(gl.GLGraphicsItem.GLGraphicsItem):
        """Gaussian splats, or a point scatter where instancing is missing.

        The fallback is a child scatter item, drawn after this one in the same
        pass, so the first captured frame already shows it. ``supported`` is
        None until the first paint probes the context, then True or False.
        """

        def __init__(self, *, point_size: float = 3.0):
            super().__init__()
            self.buffer = SplatBuffer()
            self._neural = NeuralColors()
            self.supported = None
            self.fallback_reason = None
            self.point_size = point_size
            self._fallback_colors = None
            self._program = None
            self._locations = None
            self._divisor = self._draw = None
            self._corners = QtOpenGL.QOpenGLBuffer(QtOpenGL.QOpenGLBuffer.Type.VertexBuffer)
            self._instances = QtOpenGL.QOpenGLBuffer(QtOpenGL.QOpenGLBuffer.Type.VertexBuffer)
            self._uploaded = 0
            self._fallback = Scatter(parentItem=self, pxMode=True)
            # Normal alpha blending, as the Scene's own scatter uses: additive
            # blending saturates to white on a white background.
            self._fallback.setGLOptions("translucent")
            self._fallback.setDepthValue(1)
            self._fallback.setVisible(False)

        @property
        def status(self) -> str:
            return status(self.supported, self.fallback_reason)

        def set_frame(self, frame, *, base_color, point_size=None) -> None:
            """Show a Gaussian `RenderFrame`; N may be 0 or change per frame.

            A neural appearance is evaluated here, on the caller's thread and
            for the current camera, never inside a paint.
            """
            if point_size is not None:
                self.point_size = point_size
            view = self.view()
            if view is not None:
                camera = view.cameraPosition()
                eye = np.array([camera.x(), camera.y(), camera.z()], dtype=np.float32)
            else:
                eye = np.zeros(3, dtype=np.float32)
            colors = base_colors(frame, base_color, eye, self._neural)
            self.buffer.set_frame(frame, colors)
            alpha = (frame.opacities if frame.opacities is not None
                     else np.ones(len(colors), dtype=np.float32))
            self._fallback_colors = np.column_stack([colors, alpha]).astype(np.float32)
            if self.supported is False:
                self._show_fallback()
            self.update()

        def _show_fallback(self) -> None:
            positions = self.buffer.packed[:, 0:3]
            self._fallback.setVisible(bool(len(positions)))
            if len(positions):
                self._fallback.setData(pos=np.ascontiguousarray(positions),
                                       color=self._fallback_colors, size=self.point_size)

        def _initialize(self) -> None:
            context = QtGui.QOpenGLContext.currentContext()
            vertex, fragment, self._divisor, self._draw = capabilities(context)
            # compileShader raises with the driver's log when GLSL is rejected.
            stages = [shaders.compileShader(vertex, GL.GL_VERTEX_SHADER)]
            try:
                stages.append(shaders.compileShader(fragment, GL.GL_FRAGMENT_SHADER))
                program = GL.glCreateProgram()
                for stage in stages:
                    GL.glAttachShader(program, stage)
                # Generic attribute 0 must be a per-vertex array on compatibility
                # contexts, so the corner -- never instanced -- takes it.
                for location, name in enumerate(_ATTRIBUTES):
                    GL.glBindAttribLocation(program, location, name)
                GL.glLinkProgram(program)
                for stage in stages:
                    GL.glDetachShader(program, stage)
            finally:
                for stage in stages:
                    GL.glDeleteShader(stage)
            if GL.glGetProgramiv(program, GL.GL_LINK_STATUS) != GL.GL_TRUE:
                log = GL.glGetProgramInfoLog(program)
                GL.glDeleteProgram(program)
                raise SplatsUnsupported(f"splat shader did not link: {log!r}")
            self._program = program
            self._uniforms = {name: GL.glGetUniformLocation(program, name)
                              for name in ("u_modelview", "u_projection", "u_viewport")}
            self._corners.create()
            self._corners.bind()
            self._corners.allocate(_CORNERS, _CORNERS.nbytes)
            self._corners.release()

        def paint(self):
            if self.supported is None:
                try:
                    self._initialize()
                    self.supported = True
                except Exception as error:
                    # Missing capability or a driver that rejects the shader:
                    # either way, show the centers rather than nothing.
                    self.supported = False
                    self.fallback_reason = str(error) or type(error).__name__
                    self._release_program()
                    self._show_fallback()
                    return
            if not self.supported or not len(self.buffer):
                return
            modelview = np.array(self.modelViewMatrix().data(), dtype=np.float32).reshape(4, 4)
            projection = np.array(self.projectionMatrix().data(), dtype=np.float32)
            # QMatrix4x4.data() is column-major: the reshape is the transpose.
            rows = self.buffer.instances(modelview.T)
            if rows is not None:
                self._upload(rows)
            viewport = GL.glGetIntegerv(GL.GL_VIEWPORT)
            self._draw_instances(modelview.ravel(), projection, float(viewport[2]),
                                 float(viewport[3]))

        def _upload(self, rows: np.ndarray) -> None:
            if not self._instances.isCreated():
                self._instances.create()
            self._instances.bind()
            rows = np.ascontiguousarray(rows, dtype=np.float32)
            if self._instances.size() != rows.nbytes:
                self._instances.allocate(rows, rows.nbytes)
            else:
                self._instances.write(0, rows, rows.nbytes)
            self._instances.release()
            self._uploaded = len(rows)

        def _draw_instances(self, modelview, projection, width, height) -> None:
            GL.glEnable(GL.GL_DEPTH_TEST)
            GL.glDepthMask(GL.GL_FALSE)      # splats occlude by blending only
            GL.glDisable(GL.GL_CULL_FACE)
            GL.glEnable(GL.GL_BLEND)
            GL.glBlendFuncSeparate(GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA,
                                   GL.GL_ONE, GL.GL_ONE_MINUS_SRC_ALPHA)
            enabled = []
            try:
                GL.glUseProgram(self._program)
                GL.glUniformMatrix4fv(self._uniforms["u_modelview"], 1, False, modelview)
                GL.glUniformMatrix4fv(self._uniforms["u_projection"], 1, False, projection)
                GL.glUniform2f(self._uniforms["u_viewport"], width, height)
                self._corners.bind()
                GL.glVertexAttribPointer(0, 2, GL.GL_FLOAT, False, 0, None)
                GL.glEnableVertexAttribArray(0)
                enabled.append(0)
                self._corners.release()
                self._instances.bind()
                stride = STRIDE * 4
                for location, (size, offset) in enumerate(((3, 0), (3, 3), (3, 6), (4, 9)), 1):
                    GL.glVertexAttribPointer(location, size, GL.GL_FLOAT, False, stride,
                                             ctypes.c_void_p(offset * 4))
                    GL.glEnableVertexAttribArray(location)
                    self._divisor(location, 1)
                    enabled.append(location)
                self._instances.release()
                self._draw(GL.GL_TRIANGLE_STRIP, 0, 4, self._uploaded)
            finally:
                # The default VAO is shared with every other item in the view.
                for location in enabled:
                    if location:
                        self._divisor(location, 0)
                    GL.glDisableVertexAttribArray(location)
                GL.glUseProgram(0)
                GL.glDepthMask(GL.GL_TRUE)
                GL.glBlendFunc(GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA)

        def _release_program(self) -> None:
            if self._program is not None:
                GL.glDeleteProgram(self._program)
                self._program = None

        def dispose(self) -> None:
            """Release GL objects; the owning view's context must be current."""
            self._release_program()
            for buffer in (self._corners, self._instances):
                if buffer.isCreated():
                    buffer.destroy()
            if self._fallback is not None:
                if self._fallback.program is not None:
                    GL.glDeleteProgram(self._fallback.program)
                    self._fallback.program = None
                for name in ("m_vbo_position", "m_vbo_color", "m_vbo_size"):
                    buffer = getattr(self._fallback, name, None)
                    if buffer is not None and buffer.isCreated():
                        buffer.destroy()
                # Break the parent/child cycle so nothing waits for cyclic GC.
                self._fallback.setParentItem(None)
                self._fallback = None

    return GLSplatItem
