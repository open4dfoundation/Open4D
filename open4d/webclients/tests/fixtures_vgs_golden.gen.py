"""Golden VGS1 decode results from the authoritative Python decoder.

Runs `analysis/offline_benchmark.vega_asset_splats` over a real exported frame
and dumps a sample of the decoded attributes, so the JS port can be compared
against it rather than against itself.

Regenerate:
  <env-python> tests/fixtures_vgs_golden.gen.py <frame.vgs> > tests/fixtures_vgs_golden.json
"""
import json, sys
sys.path.insert(0, '/home/ryan/4DVideoStreaming')
import ast, struct, functools
from pathlib import Path
import numpy as np

# `analysis/offline_benchmark.py` cannot be imported directly here: its module
# header pulls in the DeltaStream server, which needs PyAV. Extract the exact
# source of the VGS functions instead, so these fixtures still come from the
# authoritative implementation rather than a paraphrase of it.
SOURCE = Path('/home/ryan/4DVideoStreaming/analysis/offline_benchmark.py').read_text()
tree = ast.parse(SOURCE)
wanted = {'vega_asset_splats', 'vega_asset_cloud'}
namespace = {
    'np': np, 'struct': struct, 'Path': Path, 'functools': functools,
    'lru_cache': functools.lru_cache,
    'VGS_HEADER': struct.Struct('<4sHHII6fQ'),
}
# vega_asset_cloud returns a PointCloud; a light stand-in is enough here.
class PointCloud:
    def __init__(self, positions, colors):
        self.positions = np.asarray(positions)
        self.colors = np.asarray(colors)
namespace['PointCloud'] = PointCloud

for node in tree.body:
    if isinstance(node, ast.Assign) and any(
            getattr(t_, 'id', None) == 'VGS_HEADER' for t_ in node.targets):
        exec(compile(ast.Module([node], []), '<vgs>', 'exec'), namespace)
    if isinstance(node, ast.FunctionDef) and node.name in wanted:
        # Drop decorators (lru_cache) so the extracted function stands alone.
        node.decorator_list = []
        exec(compile(ast.Module([node], []), '<vgs>', 'exec'), namespace)
missing = wanted - set(namespace)
if missing:
    raise SystemExit(f'could not extract {missing} from offline_benchmark.py')
vega_asset_splats = namespace['vega_asset_splats']
vega_asset_cloud = namespace['vega_asset_cloud']
VGS_HEADER = namespace['VGS_HEADER']


path = sys.argv[1]
positions, scale_raw, rotation, opacity_raw, colors = vega_asset_splats(path)
cloud = vega_asset_cloud(path)

raw = open(path, 'rb').read()
header = VGS_HEADER.unpack_from(raw)

# Sample deterministically across the whole frame rather than just the head:
# a bit-shift error in a later word only shows up away from index 0.
n = positions.shape[0]
idx = sorted(set(int(i) for i in np.linspace(0, n - 1, 64).astype(int)))

def rows(a, indices):
    return [[round(float(v), 6) for v in np.atleast_1d(a[i]).ravel()] for i in indices]

print(json.dumps({
    "file": path,
    "header": {
        "magic": header[0].decode(), "version": header[1],
        "objectId": header[2], "frame": header[3], "count": header[4],
        "lower": [round(float(v), 6) for v in header[5:8]],
        "upper": [round(float(v), 6) for v in header[8:11]],
        "encodedBytes": header[11],
    },
    "visibleCount": int(n),
    "cloudCount": int(cloud.positions.shape[0]),
    "sampleIndices": idx,
    "positions": rows(positions, idx),
    "scaleRaw": rows(scale_raw, idx),
    "rotation": rows(rotation, idx),
    "opacityRaw": rows(opacity_raw, idx),
    "colors": rows(colors, idx),
    # Aggregates catch a systematic error the samples might straddle.
    "positionMean": [round(float(v), 6) for v in positions.mean(axis=0)],
    "scaleRawMean": [round(float(v), 6) for v in scale_raw.mean(axis=0)],
    "colorMean": [round(float(v), 6) for v in colors.mean(axis=0)],
}, indent=1))
