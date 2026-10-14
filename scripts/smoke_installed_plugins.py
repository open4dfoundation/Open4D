#!/usr/bin/env python3
"""Prove core/plugin installs and cold universal decode in an isolated environment."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import venv


CORE = '''
import sys, numpy as np
from pathlib import Path
import open4d
assert {c.id for c in open4d.available_codecs()} == {'npz','raw','deflate','bzip2','lzma','rle'}
assert not any(m in sys.modules for m in ('torch','open3d','scipy'))
assert not (Path(open4d.__file__).parent / 'codecs').exists()
points = open4d.PointCloud([[0.,0.,0.]], attributes={'label': np.array([17], np.int16)})
gaussians = open4d.GaussianCloud([[1.,2.,3.]], [[.1,.2,.3]], [[1.,1.,0.,0.]], [.7])
with open4d.Sequence(open4d.MemoryFrameProvider([
    open4d.Frame(7,.25,points), open4d.Frame(9,.75,gaussians)
])) as sequence:
    open4d.encode(sequence, sys.argv[1], codec='npz')
print('Standalone NumPy core encoded point and Gaussian frames.')
'''
COLD = '''
import sys, numpy as np, open4d
with open4d.decode(sys.argv[1]) as sequence:
    assert sequence.timestamps == (.25,.75)
    assert isinstance(sequence[0].geometry, open4d.PointCloud)
    assert isinstance(sequence[1].geometry, open4d.GaussianCloud)
    assert [frame.frame_index for frame in sequence] == [7,9]
    np.testing.assert_array_equal(sequence[0].geometry.attributes['label'], [17])
    np.testing.assert_array_equal(sequence[1].geometry.positions, [[1.,2.,3.]])
print('Cold process decoded universal geometry without codecs, weights or optional runtimes.')
'''
PLUGINS = '''
import sys, open4d
from pathlib import Path
from open4d.codec._api import _codec
from open4d.reconstruction import available_methods
expected={'klt','n4mc','qndf','qndf-int8','tvmc','tsmc','vdmc','faster_vdmc','queen','3dgstream','rerf','vega',
          'npz','raw','deflate','bzip2','lzma','rle'}
assert {c.id for c in open4d.available_codecs()} == expected
assert {m.id for m in available_methods()} == {'rgbd','queen','3dgstream'}
for identifier in expected:
    assert _codec(identifier,Path('unused.o4d')).id == identifier
assert not any(m in sys.modules for m in ('torch','open3d','scipy'))
assert not (Path(open4d.__file__).parent / 'codecs').exists()
print('All 12 adapter providers discovered lazily from the installed wheel.')
'''
INDIVIDUAL = '''
from open4d.codec import available_codecs
assert {c.id for c in available_codecs()} == {'npz','raw','deflate','bzip2','lzma','rle','klt'}
print('Individual adapter wheel exposes KLT without installing other research adapters.')
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-wheel", type=Path, required=True)
    parser.add_argument("--adapters", type=Path, required=True)
    args = parser.parse_args()
    core = args.core_wheel.resolve()
    adapters = args.adapters.resolve()

    def wheel(prefix):
        choices = list(adapters.glob(f"{prefix}-*.whl"))
        if len(choices) != 1:
            parser.error(f"expected one {prefix} wheel; found {choices}")
        return choices[0]

    with tempfile.TemporaryDirectory(prefix="open4d-plugin-install-") as directory:
        root = Path(directory)
        environment = root / "venv"
        venv.EnvBuilder(with_pip=True).create(environment)
        executable = environment / ("Scripts/python.exe" if __import__("os").name == "nt" else "bin/python")

        def run(*command):
            subprocess.run([str(executable), *map(str, command)], cwd=root, check=True)

        run("-m", "pip", "install", core)
        artifact = root / "geometry.o4d"
        run("-I", "-c", CORE, artifact)
        run("-I", "-c", COLD, artifact)
        run("-m", "pip", "install", "--no-index", "--no-deps", wheel("open4d_research_adapters"))
        run("-I", "-c", PLUGINS)
        run("-m", "pip", "uninstall", "-y", "open4d-research-adapters")
        run("-m", "pip", "install", "--no-index", "--no-deps", wheel("open4d_klt"))
        run("-I", "-c", INDIVIDUAL)
    print("Clean core/plugin installation proof passed.")


if __name__ == "__main__":
    main()
