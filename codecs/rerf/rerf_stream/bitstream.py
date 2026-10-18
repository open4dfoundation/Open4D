"""ReRF's bitstream, decoded and rendered frame by frame.

This is the only way this package reads a ReRF video, and that is deliberate.
Upstream can also render from *training checkpoints* -- one ``fine_last_N.tar``
per frame -- but that is not streaming: it needs every trained model on local
disk, roughly a hundred times the bitstream, which is exactly what compression
exists to avoid. The bitstream is what a client receives, so it is what a
streaming platform should be built on.

What a frame is, concretely. ReRF represents a moving scene as a 3D grid of
feature vectors plus one small neural network shared by the whole video; a
pixel is produced by marching a ray through the grid and asking the network for
colour and density at each sample. A frame of the bitstream carries:

``header_<n>.json``
    Sizes, quality, and how many voxels are occupied.
``mask_<n>.rerf``
    One bit per grid cell: is anything here at all. Empty space costs almost
    nothing, which is most of a scene.
``feature_<n>_<quality>.rerf``
    The features, compressed the way a JPEG is -- cosine transform,
    quantisation, entropy coding. Hence the quality dial, and hence the
    compiled entropy coder.
``deform_<n>.npy`` and ``deform_mask_<n>.rerf``
    Where the previous frame's contents moved to. Absent on a key frame.

**The decode is sequential, and that is a property of the format rather than of
this code.** A P-frame's features are a residual over its predecessor's, so
frame *n* means nothing until every frame since the group's key frame has been
decoded in order. :meth:`BitstreamPlayer.play` therefore yields in order, and
at the end of a group restarts from the key frame -- which is what a looping
client does, because a P-frame has nothing left to residual against.

Transcribed from upstream's ``rerf_render.py``, which is a script and not a
library: it parses ``sys.argv`` and renders at import. The codec primitives it
calls are upstream's, reached through `env`; what is rewritten here is the
loop, not the entropy coder. A transcription can decode, render, and produce
the wrong scene, so ``rerf_stream_tests`` scores a decoded frame against the
photograph -- 44 dB on ``g_basketball``, where a mis-wired decode lands under
20.
"""
from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List, Optional, Sequence
from collections.abc import Mapping

import numpy as np

from . import env
from .cameras import Camera, inward_near_far, training_cameras

#: ``--pca_chs`` in upstream's compress.py and its default. Must match what the
#: bitstream was encoded with; a mismatch decodes to noise rather than raising.
DEFAULT_PCA_CHANNELS = (7, 13)

#: ``density_act`` in upstream's codec: the raw density an absent voxel decodes
#: to, and the offset a key frame's density is coded relative to. Zero would
#: activate to a visible alpha and paint fog through empty space.
DENSITY_ACT = -4.1


class BitstreamError(ValueError):
    """A bitstream that cannot be decoded as declared, refused before decoding."""


def _feature_present(path: Path, prefix: str, channels: int) -> bool:
    """One entropy half: the coder writes ``<prefix><channel>`` per channel."""
    return all((path / ("%s%d" % (prefix, channel))).is_file() for channel in range(channels))


def validate_bitstream(path, frames: Sequence[int], *, group_size: int, pca: bool,
                       pca_channels: Sequence[int] = DEFAULT_PCA_CHANNELS,
                       n_channel: Optional[int] = None) -> None:
    """Refuse a bitstream whose structure disagrees with how it will be decoded.

    Every check here guards a decode that would otherwise *succeed* and show
    the wrong scene: a group joined mid-way, a gap that residuals across two
    frames, a residual with its motion missing (the legacy player decodes it
    as a key frame), a ``--pca``/``--pca_chs``/``--group_size`` that differs
    from the encode, or a colour network that would be fetched from wherever
    the trainer left it. Reads JSON and file names only.
    """
    path = Path(path)
    frames = list(frames)
    problems: List[str] = []
    if type(group_size) is not int or group_size <= 0:
        raise BitstreamError(f"group size must be a positive integer, not {group_size!r}")
    if not frames:
        raise BitstreamError(f"{path} holds no header_<frame>.json")
    if (any(type(index) is not int or index < 0 for index in frames)
            or frames != sorted(set(frames))):
        raise BitstreamError("ReRF frame indices must be nonnegative, unique and increasing")
    if frames[0] % group_size:
        problems.append(
            f"the stream starts at frame {frames[0]}, which is not a key frame for group "
            f"size {group_size}; its residuals have nothing to accumulate onto. Include "
            f"frames from {frames[0] - frames[0] % group_size}, or pass the group size "
            "it was encoded with"
        )
    missing = [index for a, b in zip(frames, frames[1:]) for index in range(a + 1, b)]
    if missing:
        problems.append("frames are not contiguous; missing %s" % _ranges(missing))
    for name, role in (("model_kwargs.json", "field model"), ("rgb_net.tar", "shared colour network")):
        if not (path / name).is_file():
            problems.append(f"{name} ({role}) is missing beside the bitstream")
    pca_channels = tuple(int(c) for c in pca_channels)
    if pca and (len(pca_channels) != 2 or not 0 < pca_channels[0] < pca_channels[1]):
        problems.append(f"PCA channels must be two increasing counts, not {pca_channels}")
        pca = False
    if pca and n_channel is not None and pca_channels[1] != n_channel:
        problems.append(
            f"PCA channels {pca_channels} do not cover the field's {n_channel} channels "
            "(rgbnet_dim + 1)"
        )

    for frame in frames:
        key = frame % group_size == 0
        kind = "key" if key else "residual"
        try:
            headers = json.loads((path / ("header_%d.json" % frame)).read_text())["headers"]
            channels = [int(entry["size"][1]) for entry in headers]
            quality = int(headers[0]["quality"])
            qualities = [int(entry["quality"]) for entry in headers]
        except (OSError, ValueError, KeyError, IndexError, TypeError) as error:
            problems.append(f"frame {frame}: unreadable header ({error})")
            continue
        halves = 2 if (pca and not key) else 1
        if len(headers) != halves:
            problems.append(
                f"frame {frame} is a {kind} frame for group size {group_size} with PCA "
                f"{'on' if pca else 'off'}, so it needs {halves} entropy header(s), and has "
                f"{len(headers)}; the stream was encoded with a different --group_size or --pca"
            )
            continue
        if qualities != [quality - half for half in range(halves)]:
            problems.append(f"frame {frame}: entropy header qualities {qualities} are inconsistent")
        if halves == 2:
            expected = [pca_channels[0], pca_channels[1] - pca_channels[0]]
        else:
            expected = [n_channel] if n_channel is not None else channels
        if channels != expected:
            problems.append(
                f"frame {frame}: entropy channels {channels}, expected {expected}; "
                "the stream was encoded with different --pca_chs or a different rgbnet_dim"
            )
        required = ["mask_%d.rerf" % frame]
        if halves == 2:
            required.append("pca_m_%d.npy" % frame)
        motion = ["deform_%d.npy" % frame, "deform_mask_%d.rerf" % frame]
        present = [name for name in motion if (path / name).is_file()]
        if not key:
            required += motion
        elif present:
            problems.append(
                f"frame {frame} is a key frame for group size {group_size} but carries "
                f"motion ({', '.join(present)}); the stream was encoded with a different "
                "--group_size"
            )
        absent = [name for name in required if not (path / name).is_file()]
        for half in range(halves):
            prefix = "feature_%d_%d.rerf" % (frame, quality - half)
            if not _feature_present(path, prefix, channels[half]):
                absent.append(prefix + "*")
        if absent:
            problems.append(f"{kind} frame {frame} is incomplete: missing {', '.join(absent)}")

    if problems:
        shown = problems[:12] + (["... and %d more" % (len(problems) - 12)] if len(problems) > 12 else [])
        raise BitstreamError(
            f"ReRF bitstream {path} cannot be decoded as declared:\n  - " + "\n  - ".join(shown)
            + "\nstrict=False restores the legacy player, which decodes such streams "
            "without complaint and renders whatever results."
        )


def _ranges(values: Sequence[int]) -> str:
    """``[3, 4, 5, 9]`` -> ``"3-5, 9"``."""
    spans: List[List[int]] = []
    for value in values:
        if spans and value == spans[-1][1] + 1:
            spans[-1][1] = value
        else:
            spans.append([value, value])
    return ", ".join(str(a) if a == b else f"{a}-{b}" for a, b in spans)


def packaged_camera(entry) -> Camera:
    """A camera from a packaged decoder config's ``cameras`` list."""
    return Camera(
        camera_id=int(entry["camera_id"]), width=int(entry["width"]), height=int(entry["height"]),
        fx=float(entry["fx"]), fy=float(entry["fy"]), cx=float(entry["cx"]), cy=float(entry["cy"]),
        c2w=np.asarray(entry["c2w"], dtype=np.float64),
    )


@dataclass
class DecodedFrame:
    """One frame, decoded and installed in the player's model.

    Holds the model rather than a copy of the grid: the player owns one network
    and swaps its parameters per frame, because rebuilding it costs more than
    the decode does. So this is valid only until the next frame is decoded,
    which is all a playback loop needs.
    """

    index: int
    model: object
    is_key_frame: bool
    #: Seconds the entropy decode took. The render is timed by the caller.
    decode_seconds: float = 0.0


class BitstreamPlayer:
    """A ReRF bitstream, opened once and played frame by frame.

    ``config_path`` is the run's config; the copy written inside a run
    directory is the resolved one. ``compression_path`` is the directory
    upstream's ``codec/compress.py`` wrote. ``pca`` and ``pca_channels`` have to
    match how it was encoded -- upstream's README says so, and it is worth
    repeating because the failure is a decode that succeeds and yields noise.

    ``strict`` (the default) refuses, before any kernel is built, a stream
    whose structure disagrees with that configuration -- see
    :func:`validate_bitstream` -- and never reads ``rgb_net.tar`` from the
    training run. ``strict=False`` is the legacy player: it decodes a residual
    with missing motion as a key frame, starts mid-group, and falls back to the
    training directory's colour network.

    ``config_path`` may also be a mapping: the packaged decoder configuration
    Open4D carries, whose ``render`` near/far and ``cameras`` stand in for the
    training corpus.
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
        render_bounds=None,
        strict: bool = True,
    ) -> None:
        self.path = Path(compression_path).expanduser().resolve()
        self.frames = self._available_frames()
        if not self.frames:
            raise FileNotFoundError(
                f"{self.path} holds no ReRF bitstream: no header_<frame>.json. "
                "Encode one with upstream's codec/compress.py."
            )
        self.strict = bool(strict)
        self.pca = bool(pca)
        self.pca_channels = tuple(int(channel) for channel in pca_channels)
        # Upstream: `group_size = args.group_size if given else args.frame_num`.
        # Defaulting to the number of frames present plays a single-group
        # bitstream correctly without having to be told its own structure.
        self.group_size = int(group_size) if group_size else len(self.frames)

        # Read the configuration before the runtime is activated, so a stream
        # that cannot be decoded is refused before minutes of kernel builds.
        env.patch_dependencies()
        import mmcv

        self.config_path = None if isinstance(config_path, Mapping) else Path(config_path).expanduser().resolve()
        self.cfg = mmcv.Config(dict(config_path)) if self.config_path is None else mmcv.Config.fromfile(str(self.config_path))
        datadir = self.cfg.data.get("datadir")
        # A packaged decoder configuration names no corpus.
        self.corpus_dir = Path(datadir).expanduser() if datadir else None
        self.n_channel = int(self.cfg.fine_model_and_render["rgbnet_dim"]) + 1
        self.voxel_size = int(self.cfg.voxel_size)
        if self.strict:
            validate_bitstream(
                self.path, self.frames, group_size=self.group_size, pca=self.pca,
                pca_channels=self.pca_channels, n_channel=self.n_channel,
            )
        rgb_net = self._rgb_net()

        env.activate()
        with env.upstream_cwd():
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

        # Upstream sets this before touching the codec and the codec depends on
        # it: `recover_misc` and the occupancy-cache arithmetic build tensors
        # without naming a device, so with the default left on CPU they land
        # there and every operation against a CUDA tensor raises.
        if torch.cuda.is_available():
            torch.set_default_tensor_type("torch.cuda.FloatTensor")

        self._torch = torch
        self._functional = functional
        self._dvgo = dvgo
        self._codec = {
            "recover_misc": recover_misc,
            "recover_misc_deform": recover_misc_deform,
            "decode_jpeg_huffman": decode_jpeg_huffman,
            "decode_entropy_motion_npy": decode_entropy_motion_npy,
            "unproject_pca_mmap": unproject_pca_mmap,
        }

        self.device = torch.device(device)

        with open(self.path / "model_kwargs.json") as handle:
            self.model_kwargs = json.load(handle)

        with env.upstream_cwd():
            container = dvgo_video.DirectVoxGO_Video()
            container.current_frame_id = self.frames[0]
            container.load_rgb_net_mmap(self.cfg, torch.load(str(rgb_net)))
            self.model = dvgo.DirectVoxGO(
                **dict(self.model_kwargs, rgbnet=container.rgbnet)
            )
        self.model.k0.eval()

        if render_bounds is None and "render" in self.cfg:
            render_bounds = dict(self.cfg.render)
        if render_bounds is None:
            if self.corpus_dir is None:
                raise ValueError(
                    "no render bounds: the configuration carries no render near/far "
                    "and names no corpus to derive them from"
                )
            near, far = inward_near_far(self.corpus_dir)
        else:
            near, far = render_bounds["near"], render_bounds["far"]
            if (isinstance(near, bool) or isinstance(far, bool) or
                    not np.isfinite([near, far]).all() or not 0 <= near < far):
                raise ValueError("render bounds must be finite with 0 <= near < far")
        self.render_kwargs = {
            "near": near,
            "far": far,
            "bg": 1 if self.cfg.data["white_bkgd"] else 0,
            "stepsize": self.cfg.fine_model_and_render["stepsize"],
            "inverse_y": self.cfg.data["inverse_y"],
            "flip_x": self.cfg.data["flip_x"],
            "flip_y": self.cfg.data["flip_y"],
        }

    def _rgb_net(self) -> Path:
        """The colour network: one for the whole video, shipped with the stream.

        Upstream's compress.py copies it in beside the bitstream. Strict decoding
        accepts only that copy; the legacy fallback to the training run is what
        let a "decoded" stream depend on files a receiver never has.
        """
        packaged = self.path / "rgb_net.tar"
        if packaged.is_file():
            return packaged
        if self.strict:
            raise FileNotFoundError(
                f"no rgb_net.tar beside the bitstream in {self.path}. It is the shared "
                "colour network and part of the stream: copy it from the training run "
                "(upstream's compress.py does), or pass strict=False to read it from there."
            )
        training = Path(self.cfg.basedir).expanduser() / self.cfg.expname / "rgb_net.tar"
        if not training.is_file():
            raise FileNotFoundError(f"no rgb_net.tar beside {self.path} or in the run dir")
        return training

    # ------------------------------------------------------------- the shape ---
    def _available_frames(self) -> List[int]:
        """Frame ids the bitstream carries, in numeric order.

        Numeric, not lexical: ``header_10.json`` sorts before ``header_2.json``
        as text, which would decode frame 10's residual onto frame 1's grid.
        The picture stays plausible and the motion is wrong, with nothing
        raised anywhere.
        """
        found = []
        for path in self.path.glob("header_*.json"):
            try:
                found.append(int(path.stem.split("_", 1)[1]))
            except (IndexError, ValueError):
                continue
        return sorted(found)

    def cameras(self, frame_index: Optional[int] = None) -> List[Camera]:
        """The trainer's own viewpoints, which is what renders are scored at.

        A packaged decoder configuration carries them as ``cameras``; otherwise
        they are read from the training corpus.
        """
        if "cameras" in self.cfg:
            return [packaged_camera(entry) for entry in self.cfg.cameras]
        if self.corpus_dir is None:
            raise FileNotFoundError(
                "the configuration carries no cameras and names no corpus to read them from"
            )
        return training_cameras(
            self.corpus_dir, self.frames[0] if frame_index is None else frame_index
        )

    @property
    def background(self) -> float:
        """What empty space renders as, for scoring against a photograph."""
        return float(self.render_kwargs["bg"])

    @property
    def bitstream_bytes(self) -> int:
        """Every byte of the bitstream -- the number a rate claim is made of."""
        return sum(path.stat().st_size for path in self.path.iterdir() if path.is_file())

    # ------------------------------------------------------------ the decode ---
    def _decode(self, frame_id: int, former, *, first: bool):
        """One frame's feature grid, given its predecessor's.

        ``former`` is the accumulated grid a P-frame residuals against, and is
        returned so the caller can thread it through the group. ``first``
        forces the key-frame path: a stream joined mid-group has nothing to
        accumulate onto.
        """
        from bitarray import bitarray

        torch, codec = self._torch, self._codec
        key_frame = (frame_id % self.group_size) == 0

        with open(self.path / ("header_%d.json" % frame_id)) as handle:
            headers = json.load(handle)
        header = headers["headers"][0]

        mask_size = header["mask_size"]
        # unpackbits works in whole bytes, so the bitfield is read rounded up
        # and trimmed back to the voxel count.
        padded = mask_size if mask_size % 8 == 0 else (mask_size // 8 + 1) * 8
        bits = bitarray()
        with open(self.path / ("mask_%d.rerf" % frame_id), "rb") as handle:
            bits.fromfile(handle)
        mask = torch.from_numpy(
            np.unpackbits(bits).reshape(padded)[:mask_size].astype(bool)
        ).to(self.device)

        quality = header["quality"]
        if not key_frame and self.pca:
            # PCA splits the residual across two quality levels; both halves
            # are decoded and unprojected together.
            halves = [
                codec["decode_jpeg_huffman"](
                    str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality))),
                    headers["headers"][0], device=self.device,
                ),
                codec["decode_jpeg_huffman"](
                    str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality - 1))),
                    headers["headers"][1], device=self.device,
                ),
            ]
            residual = codec["unproject_pca_mmap"](
                halves, str(self.path), frame_id, self.device, self.voxel_size
            )
        else:
            residual = codec["decode_jpeg_huffman"](
                str(self.path / ("feature_%d_%d.rerf" % (frame_id, quality))),
                header, device=self.device,
            )

        deform_mask_path = self.path / ("deform_mask_%d.rerf" % frame_id)
        deform_path = self.path / ("deform_%d.npy" % frame_id)
        if self.strict:
            # Validated when opened: residuals carry motion and the stream starts
            # at a key frame. Never reinterpret a residual as a key frame -- that
            # decodes, renders, and shows the wrong scene.
            as_key = key_frame
            if not key_frame and former is None:
                raise BitstreamError(
                    f"frame {frame_id} is a residual with no decoded predecessor; "
                    "decoding has to start at its group's key frame"
                )
        else:
            as_key = (
                key_frame or first
                or not deform_mask_path.is_file() or not deform_path.is_file()
            )
        if as_key:
            former = torch.zeros(
                (mask.size(0), self.n_channel, self.voxel_size ** 3), device=self.device
            )
            former[:, 0, :] = former[:, 0, :] + DENSITY_ACT
            former = codec["recover_misc"](
                residual, former, header, mask,
                n_channel=self.n_channel, device=self.device,
            )
        else:
            bits = bitarray()
            with open(deform_mask_path, "rb") as handle:
                bits.fromfile(handle)
            deform_mask = np.unpackbits(bits).reshape(padded)[:mask_size].astype(bool)
            motion = codec["decode_entropy_motion_npy"](
                np.load(str(deform_path)), deform_mask, self.device
            )
            if self.pca:
                # The two PCA halves' sizes are summed into the first header,
                # which is what recover_misc_deform reads.
                header["size"][1] += headers["headers"][1]["size"][1]
                header["origin_size"][0] += headers["headers"][1]["origin_size"][0]
            former = codec["recover_misc_deform"](
                residual, former, header, mask, motion, self.model_kwargs,
                n_channel=self.n_channel, device=self.device,
            )
        return former, as_key

    def _install(self, features) -> None:
        """Swap a decoded grid into the model and rebuild its occupancy cache.

        The cache is what lets the ray-march skip empty space, so it has to
        follow the frame: left stale it either paints the previous frame's
        silhouette or marches the whole volume.
        """
        torch, functional, model = self._torch, self._functional, self.model

        model.density = torch.nn.Parameter(features[:, :1])
        model.k0.k0 = torch.nn.Parameter(features[:, 1:])

        density = functional.max_pool3d(model.density, kernel_size=3, padding=1, stride=1)
        alpha = 1.0 - torch.exp(
            -functional.softplus(density + self.model_kwargs["act_shift"])
            * self.model_kwargs["voxel_size_ratio"]
        )
        occupied = (alpha >= model.mask_cache_thres).squeeze(0).squeeze(0)
        xyz_min = torch.Tensor(self.model_kwargs["xyz_min"])
        xyz_max = torch.Tensor(self.model_kwargs["xyz_max"])
        model.mask_cache.mask = occupied
        model.mask_cache.xyz2ijk_scale = (
            torch.Tensor(list(occupied.shape)) - 1
        ) / (xyz_max - xyz_min)
        model.mask_cache.xyz2ijk_shift = -xyz_min * model.mask_cache.xyz2ijk_scale

    def play(self, *, loop: bool = True) -> Iterator[DecodedFrame]:
        """Decode the bitstream in order, yielding each frame once installed.

        With ``loop`` it restarts from the key frame at the end of the group
        and never stops.
        """
        while True:
            former = None
            for position, frame_id in enumerate(self.frames):
                started = time.time()
                with self._torch.no_grad():
                    former, as_key = self._decode(frame_id, former, first=position == 0)
                    self._install(former)
                yield DecodedFrame(
                    index=frame_id, model=self.model, is_key_frame=as_key,
                    decode_seconds=time.time() - started,
                )
            if not loop:
                return

    # ------------------------------------------------------------ the render ---
    def render(self, camera: Camera, *, depth: bool = False, chunk: int = 1 << 19):
        """Ray-march the currently installed frame.

        Returns ``[H, W, 3]`` in [0, 1], or ``(colour, depth)`` when ``depth``
        is set. Chunked because one ray per pixel at 1280x960 is 1.2 million
        rays and the samples along them do not fit in memory at once.

        The depth map is **relative, not metric**: upstream accumulates
        ``weights * step_id``, so it is in ray-march steps, and it is returned
        normalised into [0, 1] with near bright -- the same convention
        upstream's own render script writes. Useful for seeing what geometry a
        frame reconstructed; not a distance in world units.
        """
        torch = self._torch
        wanted = dict(self.render_kwargs)
        if depth:
            wanted["render_depth"] = True
        with torch.no_grad():
            c2w = torch.tensor(camera.c2w, dtype=torch.float32, device="cuda")
            intrinsics = torch.tensor(
                camera.intrinsic_matrix, dtype=torch.float32, device="cuda"
            )
            origins, directions, viewdirs = self._dvgo.get_rays_of_a_view(
                H=camera.height, W=camera.width, K=intrinsics, c2w=c2w, ndc=False,
                inverse_y=self.render_kwargs["inverse_y"],
                flip_x=self.render_kwargs["flip_x"],
                flip_y=self.render_kwargs["flip_y"],
            )
            origins = origins.flatten(0, -2)
            directions = directions.flatten(0, -2)
            viewdirs = viewdirs.flatten(0, -2)
            colour_pieces, depth_pieces = [], []
            for begin in range(0, len(origins), chunk):
                result = self.model(
                    origins[begin:begin + chunk].contiguous(),
                    directions[begin:begin + chunk].contiguous(),
                    viewdirs[begin:begin + chunk].contiguous(),
                    **wanted,
                )
                colour_pieces.append(result["rgb_marched"])
                if depth:
                    depth_pieces.append(result["depth"])
            shape = (camera.height, camera.width)
            image = torch.cat(colour_pieces).reshape(*shape, 3)
            image = image.clamp(0.0, 1.0).cpu().numpy()
            if not depth:
                return image
            distance = torch.cat(depth_pieces).reshape(*shape).cpu().numpy()
        span = float(distance.max())
        # Near bright, far dark, and a flat frame stays white rather than
        # dividing by zero.
        return image, (1.0 - distance / span) if span > 0 else np.ones(shape, np.float32)
