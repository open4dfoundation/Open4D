#!/usr/bin/env python3
"""
Patch script to fix the timm package maxxvit.py bug.
This addresses the issue documented at:
https://github.com/huggingface/pytorch-image-models/issues/1530#issuecomment-2084575852
"""
import os
import sys
import shutil
from pathlib import Path

def find_timm_path():
    """Find the installed timm package path."""
    try:
        import timm
        timm_file = Path(timm.__file__)
        return timm_file.parent
    except ImportError:
        print("ERROR: timm package not found. Please install it first.")
        sys.exit(1)

def patch_maxxvit():
    """Copy the patched maxxvit.py to the timm installation."""
    # Get the script directory (should be in scripts/)
    script_dir = Path(__file__).parent
    repo_root = script_dir.parent
    
    # Source file (patched version in repo)
    source_file = repo_root / "maxxvit.py"
    if not source_file.exists():
        print(f"ERROR: Patched file not found at {source_file}")
        sys.exit(1)
    
    # Destination file (in timm package)
    timm_path = find_timm_path()
    dest_file = timm_path / "models" / "maxxvit.py"
    
    # Backup original file if it exists
    if dest_file.exists():
        backup_file = dest_file.with_suffix('.py.backup')
        if not backup_file.exists():  # Only backup once
            print(f"Creating backup: {backup_file}")
            shutil.copy2(dest_file, backup_file)
    
    # Copy patched file
    print(f"Patching: {dest_file}")
    shutil.copy2(source_file, dest_file)
    print("SUCCESS: timm package has been patched!")
    print(f"Original backed up to: {backup_file}")

if __name__ == "__main__":
    patch_maxxvit()
