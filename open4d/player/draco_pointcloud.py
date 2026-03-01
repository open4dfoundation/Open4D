import sys
from typing import Optional
import numpy as np
from PyQt6 import QtWidgets, QtCore, QtGui
import pyqtgraph.opengl as gl

from open4d.io.o4d_draco_pointcloud_io import O4DDracoPointCloudReader


class O4DDracoPlayer(QtWidgets.QMainWindow):
    """
    Qt-based viewer / player for Draco-compressed .o4d point cloud files.

    Each frame is decoded on the fly from its Draco blob before display.
    """

    def __init__(
        self,
        path: str,
        fps: float = 30.0,
        loop: bool = True,
        point_size: float = 2.0,
    ):
        super().__init__()

        self.setWindowTitle("O4D Draco PointCloud Viewer")
        self.resize(1200, 800)

        self.fps = float(fps)
        self.loop = bool(loop)
        self.dt_ms = max(1, int(1000.0 / self.fps))
        self.point_size = float(point_size)

        self.reader = O4DDracoPointCloudReader(path)
        self.reader.open()

        self.frame_ids = [fi for fi, _ in self.reader.iter_frames()]
        self.frame_ids.sort()
        if not self.frame_ids:
            raise RuntimeError("No frames found in this .o4d file")

        self.current_index = 0

        # 3-D viewport
        self.view = gl.GLViewWidget()
        self.setCentralWidget(self.view)

        grid = gl.GLGridItem()
        grid.scale(1, 1, 1)
        self.view.addItem(grid)

        # Render first frame
        pts, cols, _ = self.reader.get_frame(self.frame_ids[0])
        self.scatter = gl.GLScatterPlotItem(
            pos=pts,
            color=self._to_rgba(cols),
            size=self.point_size,
        )
        self.view.addItem(self.scatter)

        self._fit_camera(pts)
        self.view.opts["elevation"] = 20
        self.view.opts["azimuth"] = 45

        # Playback timer
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self._advance_frame)
        self.timer.start(self.dt_ms)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def closeEvent(self, event):
        self.reader.close()
        super().closeEvent(event)

    def _to_rgba(self, cols: Optional[np.ndarray]) -> Optional[np.ndarray]:
        if cols is None:
            return None
        cols = np.asarray(cols, dtype=np.float32) / 255.0
        alpha = np.ones((cols.shape[0], 1), dtype=np.float32)
        return np.hstack([cols, alpha])

    def _fit_camera(self, pts: np.ndarray):
        pts = np.asarray(pts)
        if pts.size == 0:
            return
        min_xyz = pts.min(axis=0)
        max_xyz = pts.max(axis=0)
        center = (min_xyz + max_xyz) / 2.0
        extent = max(float(np.linalg.norm(max_xyz - min_xyz)), 1e-6)
        self.view.opts["center"] = QtGui.QVector3D(
            float(center[0]), float(center[1]), float(center[2])
        )
        self.view.opts["distance"] = extent * 1.5
        self.view.update()

    # ------------------------------------------------------------------
    # Playback
    # ------------------------------------------------------------------

    def _advance_frame(self):
        fi = self.frame_ids[self.current_index]
        pts, cols, _ = self.reader.get_frame(fi)

        self.scatter.setData(
            pos=pts,
            color=self._to_rgba(cols),
            size=self.point_size,
        )

        self.current_index += 1
        if self.current_index >= len(self.frame_ids):
            if self.loop:
                self.current_index = 0
            else:
                self.timer.stop()

    # ------------------------------------------------------------------
    # Keyboard shortcuts
    # ------------------------------------------------------------------

    def keyPressEvent(self, event):
        key = event.key()
        if key == QtCore.Qt.Key.Key_Up:
            self.view.opts["distance"] *= 0.9
        elif key == QtCore.Qt.Key.Key_Down:
            self.view.opts["distance"] *= 1.1
        elif key == QtCore.Qt.Key.Key_Space:
            if self.timer.isActive():
                self.timer.stop()
            else:
                self.timer.start(self.dt_ms)
        else:
            super().keyPressEvent(event)


def play_o4d_draco_pointcloud(
    path: str,
    fps: float = 30.0,
    loop: bool = True,
    point_size: float = 2.0,
):
    """
    Launch the Draco point cloud viewer for *path*.

    Parameters
    ----------
    path : str
        Path to a Draco-encoded .o4d file.
    fps : float
        Playback speed.
    loop : bool
        Whether to loop after the last frame.
    point_size : float
        GL point diameter.
    """
    app = QtWidgets.QApplication(sys.argv)
    player = O4DDracoPlayer(path, fps=fps, loop=loop, point_size=point_size)
    player.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    o4d_path = sys.argv[1] if len(sys.argv) > 1 else "pc_draco.o4d"
    play_o4d_draco_pointcloud(o4d_path, fps=30.0)
