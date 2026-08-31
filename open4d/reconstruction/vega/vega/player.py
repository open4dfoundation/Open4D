"""Vega player (client side): fetches GOV chunks over HTTP from the chunk
server (`vega.bitstream.write_bitstream`'s output, served as static files —
see `scripts/run_live_demo.py`) and reconstructs each frame's `GaussianSet`,
mirroring exactly what `vega.filtering.apply_filtering` does server-side but
starting from the wire bytes instead of in-memory tensors.

This is the stand-in for the "HTTP Video Client" + "Rendering Pipeline" boxes
in the paper's Fig. 8 architecture diagram (§7) — the one difference from a
real mobile client is that the actual GPU work here (hash lookup + MLP +
render) runs on this workstation rather than an Android device.
"""
from __future__ import annotations

import io
import json
import urllib.request

import torch

from vega.color_encoding import ColorEncodingConfig, HierarchicalColorModel
from vega.gaussians import GaussianSet


class BitstreamClient:
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")

    def _get(self, path: str) -> bytes:
        with urllib.request.urlopen(f"{self.base_url}/{path}", timeout=10) as resp:
            return resp.read()

    def get_manifest(self) -> dict:
        return json.loads(self._get("manifest.json"))

    def get_color_model(self, device: str) -> HierarchicalColorModel:
        state = torch.load(io.BytesIO(self._get("color_model.pt")), weights_only=False)
        config: ColorEncodingConfig = state["config"]
        model = HierarchicalColorModel(config).to(device)
        model.load_state_dict(state["state_dict"])
        model._key_trained = True
        return model

    def get_frame_chunk(self, frame_idx: int) -> dict:
        return torch.load(io.BytesIO(self._get(f"frame_{frame_idx:04d}.pt")), weights_only=False)


class StreamingPlayer:
    """Maintains, per object id, the most recently received Gaussian slice
    (from either a key chunk or a later residual chunk that included that
    object). Frames are reassembled by concatenating the current slices —
    not a masked overwrite into a fixed-size buffer — because each frame's
    real RGBD point cloud is independently subsampled upstream, so per-object
    Gaussian counts drift frame to frame even though object identity stays
    consistent. This is the client-side counterpart of
    `vega.filtering.apply_filtering`.
    """

    def __init__(self, color_model: HierarchicalColorModel, device: str = "cuda"):
        self.color_model = color_model
        self.device = device
        self._object_state: dict[int, GaussianSet] = {}

    def _slice(self, nc: dict, mask: torch.Tensor, oid: int) -> GaussianSet:
        """One object's attributes, moved to the render device.

        Chunks arrive as CPU tensors, so the order of operations matters a lot
        here: masking on the CPU and then copying was costing ~200 ms per
        object (a boolean gather over every attribute, single-threaded, per
        frame) and dominated playback entirely. Two fixes:

        - when the mask selects every row — the normal case, since each scene
          object is a single Vega object (see orbitvega.prepare's
          DEFAULT_K_OBJECTS) — skip the gather and copy the tensor straight
          across as one contiguous transfer;
        - otherwise copy first and gather on the device, where the gather is
          orders of magnitude cheaper than on the host.
        """
        n = int(mask.sum())
        take_all = n == mask.numel()
        dev_mask = None if take_all else mask.to(self.device, non_blocking=True)
        # (`mask` is already on the device when it comes from `reconstruct`;
        # the `.to` above is a no-op then, and keeps this usable with a CPU
        # mask from any other caller.)

        def move(key: str) -> torch.Tensor:
            t = nc[key].to(self.device, non_blocking=True)
            return t if take_all else t[dev_mask]

        return GaussianSet(
            xyz=move("xyz"), scale_raw=move("scale_raw"),
            rot_raw=move("rot_raw"), opacity_raw=move("opacity_raw"),
            sh_dc=torch.zeros(n, 1, 3, device=self.device), sh_rest=torch.zeros(n, 0, 3, device=self.device),
            object_id=torch.full((n,), oid, dtype=torch.long, device=self.device), sh_degree=0,
        )

    def reconstruct(self, chunk: dict) -> GaussianSet:
        nc = chunk["non_color"]
        # Build the per-object masks on the render device. The chunk's
        # object_id arrives on the CPU, and comparing it there cost ~19 ms per
        # object per frame (host-side elementwise over every Gaussian, with no
        # parallelism worth the name) on top of the copy `_slice` already does.
        oid_arr = nc["object_id"].to(self.device, non_blocking=True)

        if chunk["frame_type"] == "key":
            self._object_state = {}
            for oid in torch.unique(oid_arr).tolist():
                self._object_state[oid] = self._slice(nc, oid_arr == oid, oid)
        else:
            tiny = self.color_model.new_tiny_hash(chunk["frame_idx"])
            tiny.load_state_dict(chunk["tiny_hash_state"])
            for oid in chunk["transmitted_objects"]:
                mask = oid_arr == oid
                if mask.any():
                    self._object_state[oid] = self._slice(nc, mask, oid)

        assert self._object_state, "residual chunk received before any key chunk"
        return GaussianSet.cat(list(self._object_state.values()))
