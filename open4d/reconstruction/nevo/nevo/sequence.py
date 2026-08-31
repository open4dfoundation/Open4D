"""Step 1: load a trained ReRF sequence's feature voxel grids and motion vectors.

What ReRF leaves on disk per frame, under ``<basedir>/<expname>/``:

``fine_last_0.tar``
    The I-frame. ``model_state_dict['density']`` is the raw density grid
    ``[1, 1, X, Y, Z]`` and ``['k0.k0']`` the 12-channel feature grid.
``fine_last_<n>_deform.tar``
    A P-frame's **motion vectors**: ``deformation_field``, a ``[1, 3, X, Y, Z]``
    grid of per-entry displacements that warps frame ``n-1`` towards frame
    ``n``. This is the cheap part of a P-frame -- upstream quantises it to fp16
    and drops the all-zero entries (``codec.encoder_motion``).
``fine_last_<n>.tar``
    The P-frame proper. ``['k0.k0']`` is a *residual* over ``['k0.former_k0']``
    (the motion-compensated previous grid), so the absolute feature grid this
    frame renders from is the sum of the two. ``TensorDVGORes.compute_features``
    does exactly that at sample time.

:class:`ReRFFrame` hands back the absolute grids, because that is what the
importance pass (step 2) and the renderer need; the residual/motion split is
kept alongside because that is what the *network* carries and what steps 3-6
have to packetize, drop and reconstruct.

Reconstructing a usable model from these checkpoints means repeating the
fixups upstream applies in ``run.py``'s render path -- the saved
``model_kwargs`` does not round-trip on its own (the shared colour MLP lives
in a separate file, and ``use_res``/``use_deform`` come from the config rather
than the checkpoint). :meth:`ReRFSequence.frame` is that sequence, in one
place.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np

from . import rerf_env


@dataclass
class ReRFFrame:
    """One decoded frame of a ReRF sequence."""

    index: int
    model: object
    density: "object"
    features: "object"
    residual: Optional["object"]
    motion: Optional["object"]
    xyz_min: "object"
    xyz_max: "object"
    is_key_frame: bool

    @property
    def grid_shape(self) -> Tuple[int, int, int]:
        return tuple(int(n) for n in self.density.shape[2:])

    @property
    def feature_dim(self) -> int:
        return int(self.features.shape[1])

    def raw_bytes(self) -> int:
        """Uncompressed size of the density + feature grids, fp32.

        The number the NeVo paper quotes as "~800 MB" for a frame of neural
        content.
        """
        entries = int(np.prod(self.grid_shape))
        return entries * (1 + self.feature_dim) * 4


class ReRFSequence:
    """A trained ReRF sequence on disk, indexed by frame."""

    def __init__(self, config_path, device: str = "cuda"):
        rerf_env.activate()
        with rerf_env.rerf_cwd():
            import mmcv
            import torch
            from lib import dvgo, dvgo_video

        self._torch = torch
        self._dvgo = dvgo
        self._dvgo_video = dvgo_video
        self.device = torch.device(device)
        self.config_path = Path(config_path).expanduser().resolve()
        self.cfg = mmcv.Config.fromfile(str(self.config_path))
        self.run_dir = Path(self.cfg.basedir).expanduser() / self.cfg.expname
        self.corpus_dir = Path(self.cfg.data["datadir"]).expanduser()
        if not self.run_dir.is_dir():
            raise FileNotFoundError(f"no trained run at {self.run_dir}")

        # One shared colour MLP for the whole sequence (`fix_rgbnet=True`), so
        # build the container model once and reuse its rgbnet for every frame.
        self._video_model = dvgo_video.DirectVoxGO_Video()
        self._video_model.current_frame_id = 0
        self._video_model.load_rgb_net(self.cfg)

    # ------------------------------------------------------------------ paths
    def _fine_path(self, index: int) -> Path:
        return self.run_dir / ("fine_last_%d.tar" % index)

    def _deform_path(self, index: int) -> Path:
        return self.run_dir / ("fine_last_%d_deform.tar" % index)

    def available_frames(self) -> List[int]:
        """Frame indices whose fine-stage checkpoint exists, in order."""
        found = []
        for index in range(int(self.cfg.frame_num)):
            if self._fine_path(index).is_file():
                found.append(index)
        return found

    # ------------------------------------------------------------- near / far
    def near_far(self) -> Tuple[float, float]:
        """Reproduce ``lib.load_data.inward_nearfar_heuristic`` for this corpus.

        Read off ``cams_0.json`` rather than by loading the corpus: the
        heuristic only looks at camera positions, and decoding 48 views of
        every frame to learn two scalars costs a minute and several GB.
        """
        with open(self.corpus_dir / "cams_0.json") as handle:
            frames = json.load(handle)["frames"]
        # load_NHR sorts views by file path before stacking, so the positions
        # here are the same set the trainer saw, whatever the json order.
        positions = np.asarray(
            [np.asarray(f["extrinsic"], dtype=np.float64)[:3, 3]
             for f in sorted(frames, key=lambda d: d["file"])]
        )
        distance = np.linalg.norm(positions[:, None] - positions, axis=-1)
        far = float(distance.max() * 1.4)
        return far * 0.05, far

    def render_kwargs(self) -> dict:
        near, far = self.near_far()
        return {
            "near": near,
            "far": far,
            "bg": 1 if self.cfg.data["white_bkgd"] else 0,
            "stepsize": self.cfg.fine_model_and_render["stepsize"],
            "inverse_y": self.cfg.data["inverse_y"],
            "flip_x": self.cfg.data["flip_x"],
            "flip_y": self.cfg.data["flip_y"],
        }

    # ----------------------------------------------------------------- frames
    def frame_density(self, index: int):
        """Just the raw density grid of a frame, no model construction.

        Block occupancy only needs density, and taking a union of it over a
        sequence before scoring saves rebuilding every DirectVoxGO twice.
        """
        path = self._fine_path(index)
        if not path.is_file():
            raise FileNotFoundError(f"frame {index} is not trained: {path}")
        checkpoint = self._torch.load(str(path), map_location=self.device)
        return checkpoint["model_state_dict"]["density"].detach()

    def frame(self, index: int) -> ReRFFrame:
        torch = self._torch
        path = self._fine_path(index)
        if not path.is_file():
            raise FileNotFoundError(f"frame {index} is not trained: {path}")
        checkpoint = torch.load(str(path), map_location=self.device)
        kwargs = dict(checkpoint["model_kwargs"])
        state = dict(checkpoint["model_state_dict"])

        deform_path = self._deform_path(index)
        is_key_frame = not deform_path.is_file()

        # Mirrors run.py's render path: the checkpoint carries neither the
        # shared MLP nor the residual/deform switches, and `deform_res_mode ==
        # "separate"` means every frame's grid is a residual model even though
        # `cfg.use_res` is left False for the trainer's own bookkeeping.
        kwargs["rgbnet"] = self._video_model.rgbnet
        kwargs["cfg"] = self.cfg
        kwargs["use_res"] = bool(self.cfg.use_res) or self.cfg.deform_res_mode == "separate"
        kwargs["use_deform"] = ""
        kwargs["rgbfeat_sigmoid"] = self.cfg.codec["rgbfeat_sigmoid"]

        model = self._dvgo.DirectVoxGO(**kwargs)
        if kwargs["use_res"] and "k0.former_k0" not in state:
            # The I-frame has no predecessor to residual against, so its k0 *is*
            # the absolute grid and former_k0 stays at the zeros the constructor
            # made.
            state["k0.former_k0"] = model.k0.former_k0
        model.load_state_dict(state, strict=False)
        if kwargs["use_res"]:
            model.k0.former_k0_cur = model.k0.former_k0
        model = model.to(self.device).eval()

        residual = state["k0.k0"].detach()
        former = state["k0.former_k0"].detach()
        features = residual + former
        motion = None
        if not is_key_frame:
            deform = torch.load(str(deform_path), map_location=self.device)
            motion = deform["model_state_dict"]["deformation_field"].detach()

        return ReRFFrame(
            index=index,
            model=model,
            density=state["density"].detach(),
            features=features,
            residual=None if is_key_frame else residual,
            motion=motion,
            xyz_min=model.xyz_min.detach(),
            xyz_max=model.xyz_max.detach(),
            is_key_frame=is_key_frame,
        )
