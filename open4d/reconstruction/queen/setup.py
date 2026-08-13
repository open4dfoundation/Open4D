import os
from setuptools import setup, find_packages

# Read the contents of README file
with open(os.path.join(os.path.dirname(__file__), "README.md"), encoding="utf-8") as f:
    long_description = f.read()

setup(
    name="queen",
    version="1.0.0",
    description="QUEEN: QUantized Efficient ENcoding for Streaming Free-viewpoint Videos",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Sharath Girish, Tianye Li, Amrita Mazumdar, et al.",
    url="https://github.com/NVlabs/queen",
    packages=find_packages(),
    python_requires=">=3.11",
    install_requires=[
        "torch",
        "torchvision",
        "plyfile",
        "tqdm",
        "numpy",
        "pillow",
        "opencv-python-headless",
        "scipy",
        "wandb",
        "torchmetrics",
        "imutils",
        "matplotlib",
        "torchac",
        "timm==0.6.13",
        "einops==0.6.0",
    ],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Science/Research",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
        "License :: Other/Proprietary License",
        "Programming Language :: Python :: 3.11",
    ],
)
