"""ReRF's own bitstream, decoded frame by frame as it is played.

`sequence.ReRFSequence` reads *training checkpoints* -- one ``fine_last_N.tar``
per frame, which is what the importance scorer and the offline renderer need.
This reads the encoded bitstream instead: the ``header_N.json``, ``mask_N.rerf``,
``feature_N_*.rerf`` and ``deform_N.npy`` files that ``codec/compress.py``
writes and that a ReRF *client* actually receives.

That difference is the whole point of the module, and it is not cosmetic:

* **Rendering from checkpoints is not streaming.** It needs every trained model
  on local disk -- 30 frames of ``g_basketball`` is 1.7 GB of ``.tar`` against
  17 MB of bitstream, a hundredfold -- which is precisely what compression
  exists to avoid. Rendering from the bitstream is what a client does.
* **It is the only path that still works here.** The trained checkpoints for
  this repository's ReRF runs were deleted once their bitstreams existed, so
  there is nothing left for the checkpoint path to load. The bitstreams are on
  disk and complete.
* **It makes a live pane honest.** Entropy decode, residual accumulation and
  ray-march all happen while you watch, which is what lets a clip claim
  ``origin="rendered"`` rather than replaying pixels rendered hours ago.

**The decode is sequential, and that is not an implementation detail.** A
P-frame's feature grid is a residual over its predecessor's, so frame *n* means
nothing until every frame from the group's key frame has been decoded in order
-- `open4d.core.DependencyMode.SEQUENTIAL`, declared rather than discovered.
:meth:`BitstreamPlayer.play` therefore yields in order and, at the end of a
group, restarts from the key frame. That is not a limitation worked around; it
is what a looping client does, because a P-frame has nothing to residual
against once the group ends.

Transcribed from ``rerf/rerf_render.py``'s ``mmap_decode`` and
``model_callback``, which are a script and not a library: that file parses
``sys.argv`` and renders at import. The codec primitives it calls are
upstream's, imported through `rerf_env` -- what is transcribed here is the
loop, not the entropy coder.

A decoded frame duck-types `sequence.ReRFFrame` closely enough for
`render.render_view`, and a player duck-types `sequence.ReRFSequence`, so the
render path this module feeds is the same validated one the offline renderer
uses rather than a second copy of it.

Runs in the ``nevo`` environment (Python 3.8), like everything that touches a
ReRF model.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List, Optional, Sequence, Tuple

import numpy as np

from . import rerf_env
from .bitstream import DENSITY_ACT
from .cameras import Camera
from .render import training_cameras
from .sequence import inward_near_far

#: ``--pca_chs`` in compress.py and rerf_render.py, and upstream's default.
#: Must match what the bitstream was encoded with; a mismatch decodes to noise
#: rather than to an error.
DEFAULT_PCA_CHANNELS = (7, 13)


@dataclass
class DecodedFrame:
    """One frame's feature grid, decoded and installed in the player's model.

    Holds the *model*, not a copy of the grid: the player owns one
    ``DirectVoxGO`` and swaps its parameters per frame, because rebuilding it
    would cost more than the decode. So a `DecodedFrame` is only valid until
    the next one is decoded, which is all a playback loop needs and is why this
    is not `sequence.ReRFFrame` -- that one owns its tensors and can be kept.
    """

    index: int
    model: object
    is_key_frame: bool
    #: Wall-clock seconds this frame's entropy decode took, for the status line
    #: a live stream shows. Not the render, which the caller times.
    decode_seconds: float = 0.0


class BitstreamPlayer:
    """A ReRF bitstream, opened once and decoded frame by frame.

    ``config_path`` is the run's config (the copy inside a run directory is the
    resolved one). ``compression_path`` is the directory ``compress.py`` wrote.
    ``pca`` and ``pca_channels`` must match how it was encoded -- upstream's
    README says so and it is worth repeating, because the failure is a decode
    that succeeds and yields noise.
    """

    def __init__(
        self,
        config_path,
        compression_path,
        *,
        pca: bool = True,
        pca_channels: Sequence[int] = DEFAULT_PCA_CHANNELS,
        group_size: Optional[int] = None,
        device: str = "cuda",
    ) -> None:
        rerf_env.activate()
        with rerf_env.rerf_cwd():
            import mmcv
            import torch
            import torch.nn.functional as functional
            from codec import (
                decode_entropy_motion_npy,
                decode_jpeg_huffman,
                recover_misc,
                recover_misc_deform,
                unproject_pca_mmap,
            )
            from lib import dvgo, dvgo_video

        # Upstream sets this before touching the codec, and the codec relies on
        # it: `recover_misc` and the mask-cache arithmetic build tensors without
        # naming a device, so with the default left on CPU they land there and
        # every operation against a CUDA tensor raises. Process-global, which is
        # why it is done here rather than per call.
        if torch.cuda.is_available():
            torch.set_default_tensor_type("torch.cuda.FloatTensor")

        self._torch = torch
        self._functional = functional
        self._codec = {
            "recover_misc": recover_misc,
            "recover_misc_deform": recover_misc_deform,
            "decode_jpeg_huffman": decode_jpeg_huffman,
            "decode_entropy_motion_npy": decode_entropy_motion_npy,
            "unproject_pca_mmap": unproject_pca_mmap,
        }

        self.device = torch.device(device)
        self.config_path = Path(config_path).expanduser().resolve()
        self.path = Path(compression_path).expanduser().resolve()
        self.cfg = mmcv.Config.fromfile(str(self.config_path))
        self.corpus_dir = Path(self.cfg.data["datadir"]).expanduser()
        self.pca = bool(pca)
        self.pca_channels = tuple(int(channel) for channel in pca_channels)

        self.n_channel = int(self.cfg.fine_model_and_render["rgbnet_dim"]) + 1
        self.voxel_size = int(self.cfg.voxel_size)

        self.frames = self._available_frames()
        if not self.frames:
            raise FileNotFoundError(
                f"{self.path} holds no ReRF bitstream: no header_<frame>.json. "
                "Encode one with `python -m orbitnevo.rerf_cli codec/compress.py`."
            )
        # Upstream: `group_size = args.group_size if != -1 else args.frame_num`.
        # Defaulting to the number of frames present makes a single-group
        # bitstream -- which is what this repository encodes -- play correctly
        # without having to be told its own structure.
        self.group_size = int(group_size) if group_size else len(self.frames)

        with open(self.path / "model_kwargs.json") as handle:
            self.model_kwargs = json.load(handle)

        # One shared colour MLP for the whole sequence, exactly as the trainer
        # had it: `fix_rgbnet=True` means it is not per frame, and it ships
        # beside the bitstream rather than inside any frame.
        rgb_net_path = self.path / "rgb_net.tar"
        if not rgb_net_path.is_file():
            rgb_net_path = (
                Path(self.cfg.basedir).expanduser() / self.cfg.expname / "rgb_net.tar"
            )
        if not rgb_net_path.is_file():
            raise FileNotFoundError(f"no rgb_net.tar beside {self.path} or in the run dir")
        with rerf_env.rerf_cwd():
            video_model = dvgo_video.DirectVoxGO_Video()
            video_model.current_frame_id = self.frames[0]
            video_model.load_rgb_net_mmap(self.cfg, torch.load(str(rgb_net_path)))
            self.model = dvgo.DirectVoxGO(
                **dict(self.model_kwargs, rgbnet=video_model.rgbnet)
            )
        self.model.k0.eval()

        near, far = inward_near_far(self.corpus_dir)
        self._render_kwargs = {
            "near": near,
            "far": far,
            "bg": 1 if self.cfg.data["white_bkgd"] else 0,
            "stepsize": self.cfg.fine_model_and_render["stepsize"],
            "inverse_y": self.cfg.data["inverse_y"],
            "flip_x": self.cfg.data["flip_x"],
            "flip_y": self.cfg.data["flip_y"],
        }

    # --------------------------------------------------------------- the shape
    def _available_frames(self) -> List[int]:
        """Frame ids the bitstream carries, in order."""
        found = []
        for path in self.path.glob("header_*.json"):
            try:
                found.append(int(path.stem.split("_", 1)[1]))
            except (IndexError, ValueError):
                continue
        return sorted(found)

    def render_kwargs(self) -> dict:
        """Duck-types `sequence.ReRFSequence`, so `render.render_view` works."""
        return dict(self._render_kwargs)

    def cameras(self, frame_index: Optional[int] = None) -> List[Camera]:
        """The trainer's own viewpoints, which is what the renders compare at."""
        return training_cameras(
            self.corpus_dir, self.frames[0] if frame_index is None else frame_index
        )

    @property
    def bitstream_bytes(self) -> int:
        """Every byte of the bitstream, for a status line that quotes its cost."""
        return sum(path.stat().st_size for path in self.path.iterdir() if path.is_file())

    # -------------------------------------------------------------- the decode
    def _decode(self, frame_id: int, former, *, first: bool):
        """One frame's feature grid, given its predecessor's.

        ``former`` is the accumulated grid a P-frame residuals against, and is
        returned so the caller threads it through the group. ``first`` forces
        the key-frame path: a stream joined mid-group has nothing to accumulate
        onto, which upstream handles with the same ``frame_count == 0`` test.
        """
        torch = self._torch
        codec = self._codec
        from bitarray import bitarray

        key_frame = (frame_id % self.group_size) == 0
        with open(self.path / ("header_%d.json" % frame_id)) as handle:
            headers = json.load(handle)
        header = headers["headers"][0]

        mask_size = header["mask_size"]
        # unpackbits works in whole bytes, so the bitfield is read rounded up
        # and then trimmed back to the voxel count.
        padded = mask_size if mask_size % 8 == 0 else (mask_size // 8 + 1) * 8
        bits = bitarray()
        with open(self.path / ("mask_%d.rerf" % frame_id), "rb") as handle:
            bits.fromfile(handle)
        mask = torch.from_numpy(
            np.unpackbits(bits).reshape(padded)[:mask_size].astype(bool)
        ).to(self.device)

        quality = header["quality"]
        if not key_frame and self.pca:
            # PCA splits the residual across two quality levels; both halves are
            # decoded and then unprojected together.
            pieces = [
                codec["decode_jpeg_huffman"](
                    str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality))),
                    headers["headers"][0],
                    device=self.device,
                ),
                codec["decode_jpeg_huffman"](
                    str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality - 1))),
                    headers["headers"][1],
                    device=self.device,
                ),
            ]
            residual = codec["unproject_pca_mmap"](
                pieces, str(self.path), frame_id, self.device, self.voxel_size
            )
        else:
            residual = codec["decode_jpeg_huffman"](
                str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality))),
                header,
                device=self.device,
            )

        deform_mask_path = self.path / ("deform_mask_%d.rerf" % frame_id)
        deform_path = self.path / ("deform_%d.npy" % frame_id)
        as_key = (
            key_frame
            or first
            or not deform_mask_path.is_file()
            or not deform_path.is_file()
        )
        if as_key:
            # No predecessor to residual against, so the grid starts at what an
            # absent voxel decodes to and the frame is absolute.
            former = torch.zeros(
                (mask.size(0), self.n_channel, self.voxel_size ** 3), device=self.device
            )
            former[:, 0, :] = former[:, 0, :] + DENSITY_ACT
            former = self._codec["recover_misc"](
                residual, former, header, mask,
                n_channel=self.n_channel, device=self.device,
            )
        else:
            bits = bitarray()
            with open(deform_mask_path, "rb") as handle:
                bits.fromfile(handle)
            deform_mask = np.unpackbits(bits).reshape(padded)[:mask_size].astype(bool)
            deform = codec["decode_entropy_motion_npy"](
                np.load(str(deform_path)), deform_mask, self.device
            )
            if self.pca:
                # The two PCA halves' sizes are summed into the first header,
                # which is what recover_misc_deform reads.
                header["size"][1] += headers["headers"][1]["size"][1]
                header["origin_size"][0] += headers["headers"][1]["origin_size"][0]
            former = codec["recover_misc_deform"](
                residual, former, header, mask, deform, self.model_kwargs,
                n_channel=self.n_channel, device=self.device,
            )
        return former, as_key

    def _install(self, features) -> None:
        """Swap a decoded grid into the model, and rebuild its occupancy cache.

        The cache is what lets the ray-march skip empty space, so it has to
        follow the frame: left stale it either paints the previous frame's
        silhouette or marches through the whole volume.
        """
        torch = self._torch
        functional = self._functional
        model = self.model

        model.density = torch.nn.Parameter(features[:, :1])
        model.k0.k0 = torch.nn.Parameter(features[:, 1:])

        density = functional.max_pool3d(
            model.density, kernel_size=3, padding=1, stride=1
        )
        alpha = 1.0 - torch.exp(
            -functional.softplus(density + self.model_kwargs["act_shift"])
            * self.model_kwargs["voxel_size_ratio"]
        )
        mask = (alpha >= model.mask_cache_thres).squeeze(0).squeeze(0)
        xyz_min = torch.Tensor(self.model_kwargs["xyz_min"])
        xyz_max = torch.Tensor(self.model_kwargs["xyz_max"])
        model.mask_cache.mask = mask
        model.mask_cache.xyz2ijk_scale = (
            torch.Tensor(list(mask.shape)) - 1
        ) / (xyz_max - xyz_min)
        model.mask_cache.xyz2ijk_shift = -xyz_min * model.mask_cache.xyz2ijk_scale

    def play(self, *, loop: bool = True) -> Iterator[DecodedFrame]:
        """Decode the bitstream in order, yielding each frame once installed.

        With ``loop`` it restarts from the key frame at the end of the group and
        never stops, which is what a looping client does: the group's first
        frame is the only one that decodes without a predecessor.
        """
        import time

        while True:
            former = None
            for position, frame_id in enumerate(self.frames):
                started = time.time()
                with self._torch.no_grad():
                    former, as_key = self._decode(
                        frame_id, former, first=position == 0
                    )
                    self._install(former)
                yield DecodedFrame(
                    index=frame_id,
                    model=self.model,
                    is_key_frame=as_key,
                    decode_seconds=time.time() - started,
                )
            if not loop:
                return
