import sys
import numpy as np
from PyQt6 import QtWidgets, QtCore, QtGui
import pyqtgraph.opengl as gl

from open4d.io.o4d_pointcloud_io import O4DPointCloudReader

class O4DPlayer(QtWidgets.QMainWindow):
    def __init__(self, path: str, fps: float = 30.0, loop: bool = True, point_size: float = 2.0):
        super().__init__()

        self.setWindowTitle("O4D PointCloud Viewer")
        self.resize(1200, 800)

        self.fps = float(fps)
        self.loop = bool(loop)
        self.dt_ms = max(1, int(1000.0 / self.fps))
        self.point_size = float(point_size)

        self.reader = O4DPointCloudReader(path)
        self.reader.open()

        self.frame_ids = [fi for fi, _ in self.reader.iter_frames()]
        self.frame_ids.sort()
        if not self.frame_ids:
            raise RuntimeError("No frames found in this .o4d file")

        self.current_index = 0

        # Viewer
        self.view = gl.GLViewWidget()
        self.setCentralWidget(self.view)

        grid = gl.GLGridItem()
        grid.scale(1, 1, 1)
        self.view.addItem(grid)

        # First frame
        pts, cols, _ = self.reader.get_frame(self.frame_ids[0])

        self.scatter = gl.GLScatterPlotItem(
            pos=pts,
            color=self._to_rgba(cols),
            size=self.point_size
        )
        self.view.addItem(self.scatter)

        # Proper auto camera setup
        self._fit_camera_to_points(pts)

        # Nice initial angle
        self.view.opts['elevation'] = 20
        self.view.opts['azimuth'] = 45

        # Playback timer
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_frame)
        self.timer.start(self.dt_ms)

    def closeEvent(self, event):
        self.reader.close()
        super().closeEvent(event)

    def _to_rgba(self, cols):
        if cols is None:
            return None
        cols = np.asarray(cols, dtype=np.float32) / 255.0
        alpha = np.ones((cols.shape[0], 1), dtype=np.float32)
        return np.hstack([cols, alpha])

    def _fit_camera_to_points(self, pts):
        pts = np.asarray(pts)
        if pts.size == 0:
            return

        min_xyz = pts.min(axis=0)
        max_xyz = pts.max(axis=0)

        center = (min_xyz + max_xyz) / 2.0
        extent = np.linalg.norm(max_xyz - min_xyz)
        extent = max(extent, 1e-6)

        # SAFE method across all pyqtgraph versions
        self.view.opts['center'] = QtGui.QVector3D(
            float(center[0]),
            float(center[1]),
            float(center[2])
        )

        self.view.opts['distance'] = extent * 1.5

        self.view.update()

    def update_frame(self):
        fi = self.frame_ids[self.current_index]
        pts, cols, _ = self.reader.get_frame(fi)

        self.scatter.setData(
            pos=pts,
            color=self._to_rgba(cols),
            size=self.point_size
        )

        self.current_index += 1
        if self.current_index >= len(self.frame_ids):
            if self.loop:
                self.current_index = 0
            else:
                self.timer.stop()

    # Optional keyboard zoom
    def keyPressEvent(self, event):
        if event.key() == QtCore.Qt.Key.Key_Up:
            self.view.opts['distance'] *= 0.9
        elif event.key() == QtCore.Qt.Key.Key_Down:
            self.view.opts['distance'] *= 1.1
        else:
            super().keyPressEvent(event)


def play_o4d_pointcloud(path: str, fps: float = 30.0, loop: bool = True, point_size: float = 2.0):
    app = QtWidgets.QApplication(sys.argv)
    player = O4DPlayer(path, fps=fps, loop=loop, point_size=point_size)
    player.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    o4d_path = sys.argv[1] if len(sys.argv) > 1 else "pc_file.o4d"
    play_o4d_pointcloud(o4d_path, fps=30.0)
