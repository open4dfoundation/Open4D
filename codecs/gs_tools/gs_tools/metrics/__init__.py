"""One image-quality implementation for both methods.

`image.evaluate` reports PSNR under both upstream conventions, because they do not
agree: QUEEN quantizes to 8 bits before taking the MSE, 3DGStream does not.
"""

from . import image

__all__ = ["image"]
