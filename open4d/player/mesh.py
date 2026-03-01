import sys
import numpy as np
from PyQt6 import QtWidgets, QtCore, QtGui
import pyqtgraph.opengl as gl

from open4d.io.o4d_mesh_io import O4DMeshReader


class O4DMeshPlayer(QtWidgets.QMainWindow):
    def __init__(self, path: str, fps: float = 30.0, loop: bool = True):
        super().__init__()

        self.setWindowTitle("O4D Mesh Viewer")
        self.resize(1200, 800)

        self.fps = float(fps)
        self.loop = bool(loop)
        self.dt_ms = max(1, int(1000.0 / self.fps))

        self.reader = O4DMeshReader(path)
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
        verts, faces, _ = self.reader.get_frame(self.frame_ids[0])

        mesh_data = gl.MeshData(vertexes=verts, faces=faces)
        self.mesh_item = gl.GLMeshItem(
            meshdata=mesh_data,
            smooth=True,
            drawFaces=True,
            drawEdges=False,
            color=(0.6, 0.8, 1.0, 1.0),
            shader="normalColor",
        )
        self.view.addItem(self.mesh_item)

        self._fit_camera_to_vertices(verts)

        self.view.opts['elevation'] = 20
        self.view.opts['azimuth'] = 45

        # Playback timer
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_frame)
        self.timer.start(self.dt_ms)

    def closeEvent(self, event):
        self.reader.close()
        super().closeEvent(event)

    def _fit_camera_to_vertices(self, verts):
        verts = np.asarray(verts)
        if verts.size == 0:
            return

        min_xyz = verts.min(axis=0)
        max_xyz = verts.max(axis=0)

        center = (min_xyz + max_xyz) / 2.0
        extent = np.linalg.norm(max_xyz - min_xyz)
        extent = max(extent, 1e-6)

        self.view.opts['center'] = QtGui.QVector3D(
            float(center[0]),
            float(center[1]),
            float(center[2])
        )
        self.view.opts['distance'] = extent * 1.5
        self.view.update()

    def update_frame(self):
        fi = self.frame_ids[self.current_index]
        verts, faces, _ = self.reader.get_frame(fi)

        mesh_data = gl.MeshData(vertexes=verts, faces=faces)
        self.mesh_item.setMeshData(meshdata=mesh_data)

        self.current_index += 1
        if self.current_index >= len(self.frame_ids):
            if self.loop:
                self.current_index = 0
            else:
                self.timer.stop()

    def keyPressEvent(self, event):
        if event.key() == QtCore.Qt.Key.Key_Up:
            self.view.opts['distance'] *= 0.9
        elif event.key() == QtCore.Qt.Key.Key_Down:
            self.view.opts['distance'] *= 1.1
        else:
            super().keyPressEvent(event)


def play_o4d_mesh(path: str, fps: float = 30.0, loop: bool = True):
    app = QtWidgets.QApplication(sys.argv)
    player = O4DMeshPlayer(path, fps=fps, loop=loop)
    player.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    o4d_path = sys.argv[1] if len(sys.argv) > 1 else "mesh_file.o4d"
    play_o4d_mesh(o4d_path, fps=30.0)
