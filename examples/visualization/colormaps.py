"""The sequential colour ramp for a scalar error field.

Error magnitude is a sequential quantity, so the ramp is monotone in lightness —
that is what makes "further along the ramp" readable as "more", and
`tests/test_compare.py` asserts it numerically rather than trusting the eye.
Lightness rises from a deep violet to a pale yellow while hue sweeps once without
turning back, so it reads as heat. A ramp that cycles hue more than once — any
rainbow — turns a narrow band of real values into confetti and invents boundaries
in it.

Because lightness carries the magnitude it must not *also* carry the lighting: a
shaded surface would make a dark region ambiguous between "in shadow" and "high
error". The comparison viewer therefore renders an error pane nearly unshaded
(`--error-shading`, default low) so lightness is data alone.

    rgb = colorize(distances, 0.0, clamp)   # (N, 3) float32
"""

from __future__ import annotations

import numpy as np

LUT_SIZE = 256

# Colour shown where a value is not a number — a vertex whose distance could not
# be measured. Deliberately off the ramp so it cannot be misread as data.
NO_DATA = (0.45, 0.45, 0.48)

# Evenly spaced anchors, ascending in luminance. Luminance is a linear function of
# RGB, so interpolating RGB linearly interpolates luminance linearly: the ramp is
# monotone everywhere, not merely at the anchors.
_ANCHORS = np.array(
    [
        (0.13, 0.10, 0.38),
        (0.55, 0.12, 0.42),
        (0.83, 0.24, 0.24),
        (0.97, 0.51, 0.10),
        (1.00, 0.93, 0.62),
    ]
)


def lookup_table(size: int = LUT_SIZE) -> np.ndarray:
    """Sample the ramp into an (size, 3) float32 table."""
    stops = np.linspace(0.0, 1.0, len(_ANCHORS))
    fraction = np.linspace(0.0, 1.0, size)
    table = np.stack(
        [np.interp(fraction, stops, _ANCHORS[:, channel]) for channel in range(3)],
        axis=-1,
    )
    return np.clip(table, 0.0, 1.0).astype(np.float32)


def relative_luminance(rgb: np.ndarray) -> np.ndarray:
    """Rec. 709 relative luminance, for checking that the ramp is monotone."""
    weights = np.asarray([0.2126, 0.7152, 0.0722])
    return np.asarray(rgb, dtype=np.float64) @ weights


def normalize(values: np.ndarray, low: float, high: float) -> np.ndarray:
    """Map `values` onto 0..1, clamping outside `low`..`high`.

    A degenerate range — every value identical, as when two meshes match
    exactly — maps to 0 rather than dividing by zero, so a perfect match paints
    as "no error" instead of as noise.
    """
    values = np.asarray(values, dtype=np.float64)
    span = float(high) - float(low)
    if not np.isfinite(span) or span <= 0.0:
        return np.zeros(values.shape, dtype=np.float64)
    return np.clip((values - float(low)) / span, 0.0, 1.0)


def colorize(values: np.ndarray, low: float, high: float) -> np.ndarray:
    """Colour a scalar field, returning (N, 3) float32 in 0..1.

    Values above `high` take the top colour rather than being dropped, which is
    why the viewer's colourbar labels that end as a clamp.
    """
    values = np.asarray(values, dtype=np.float64)
    table = lookup_table()
    fraction = normalize(np.nan_to_num(values, nan=0.0), low, high)
    index = np.minimum((fraction * (len(table) - 1)).astype(np.int64), len(table) - 1)
    colors = table[index]
    missing = ~np.isfinite(values)
    if missing.any():
        colors = colors.copy()
        colors[missing] = NO_DATA
    return colors


def colorbar_strip(width: int, height: int) -> np.ndarray:
    """A left-to-right gradient of the ramp, as (height, width, 3) uint8.

    Shared by the Qt colourbar widget and the GIF writer so the legend in a saved
    animation is the same one that was on screen.
    """
    table = lookup_table()
    fraction = np.linspace(0.0, 1.0, max(int(width), 1))
    index = np.minimum((fraction * (len(table) - 1)).astype(np.int64), len(table) - 1)
    row = (table[index] * 255.0).round().astype(np.uint8)
    return np.repeat(row[None, :, :], max(int(height), 1), axis=0)
