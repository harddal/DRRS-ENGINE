"""
batch_convert.py — Batch-convert FBX files to GLB and merge animations.

Expected input folder layout (one subfolder per character):

    fbx/
        soldier/
            soldier.fbx        <- mesh  (file whose stem matches the folder name)
            idle.fbx           <- animations (everything else)
            walk.fbx
            run.fbx
        zombie/
            zombie.fbx
            idle.fbx
            walk.fbx

Output (flat, ready to drop into your content folder):

    glb/
        soldier.glb
        soldier.anim
        zombie.glb
        zombie.anim

Usage:
    python batch_convert.py <fbx_dir> <glb_dir>
    python batch_convert.py fbx/ glb/ --blender "C:/Program Files/Blender Foundation/Blender 4.2/blender.exe"
    python batch_convert.py fbx/ glb/ --keep-temp       # keep per-character temp GLBs for inspection
    python batch_convert.py fbx/ glb/ --scale 10    # default, adjust if needed

Dependencies:
    pip install pygltflib numpy      (for merge_anims.py)
    Blender 3.3+ installed
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT_DIR    = Path(__file__).parent
BLENDER_INNER = SCRIPT_DIR / "blender_fbx_to_glb.py"
MERGE_ANIMS   = SCRIPT_DIR / "merge_anims.py"


# ---------------------------------------------------------------------------
# Blender discovery
# ---------------------------------------------------------------------------

def find_blender(hint: str | None) -> str:
    if hint:
        return hint
    found = shutil.which("blender")
    if found:
        return found
    patterns = [
        "C:/Program Files/Blender Foundation/Blender */blender.exe",
        "D:/Projects/Game Engine/Tools/blender-4.2.19-windows-x64/blender.exe",
        "/usr/bin/blender",
        "/Applications/Blender.app/Contents/MacOS/Blender",
    ]
    for pattern in patterns:
        matches = sorted(glob.glob(pattern), reverse=True)
        if matches:
            return matches[0]
    return None


# ---------------------------------------------------------------------------
# FBX → GLB (single file via Blender subprocess)
# ---------------------------------------------------------------------------

def convert_one(blender: str, fbx: Path, glb: Path,
                scale: float = 1.0, no_animations: bool = False,
                flip_y: bool = False) -> bool:
    glb.parent.mkdir(parents=True, exist_ok=True)
    cmd = [blender, "--background", "--python", str(BLENDER_INNER),
           "--", str(fbx), str(glb)]
    if scale != 1.0:
        cmd += ["--scale", str(scale)]
    if no_animations:
        cmd += ["--no-animations"]
    if flip_y:
        cmd += ["--flip-y"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ERROR: {fbx.name}", file=sys.stderr)
        # Print last 20 lines of Blender's stderr so the cause is visible
        for line in result.stderr.splitlines()[-20:]:
            print(f"    {line}", file=sys.stderr)
        return False
    print(f"  {fbx.name}  ->  {glb.name}")
    return True


# ---------------------------------------------------------------------------
# merge_anims step
# ---------------------------------------------------------------------------

def merge_character(char_name: str, mesh_glb: Path, anim_glbs: list[Path], out_glb: Path) -> bool:
    out_glb.parent.mkdir(parents=True, exist_ok=True)
    inputs = [str(mesh_glb)] + [str(p) for p in sorted(anim_glbs)]
    result = subprocess.run(
        [sys.executable, str(MERGE_ANIMS)] + inputs + ["-o", str(out_glb)],
        capture_output=True,
        text=True,
    )
    print(result.stdout.rstrip())
    if result.returncode != 0:
        print(f"  ERROR merging {char_name}:", file=sys.stderr)
        print(result.stderr[-2000:], file=sys.stderr)
        return False
    return True


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------

def process_character(char_dir: Path, out_dir: Path, blender: str, temp_root: Path,
                      scale: float = 1.0, flip_y: bool = False) -> bool:
    char_name = char_dir.name
    fbx_files = sorted({p.resolve() for p in char_dir.glob("*.fbx")} |
                       {p.resolve() for p in char_dir.glob("*.FBX")})
    if not fbx_files:
        print(f"  SKIP {char_name}: no FBX files found")
        return True

    # Identify the mesh FBX: file whose stem matches the folder name (case-insensitive).
    # Fall back to the alphabetically first file if no match.
    mesh_fbx = next(
        (f for f in fbx_files if f.stem.lower() == char_name.lower()),
        fbx_files[0],
    )
    anim_fbxes = [f for f in fbx_files if f != mesh_fbx]

    print(f"\n[{char_name}]  mesh: {mesh_fbx.name}  +  {len(anim_fbxes)} animation(s)")

    temp_dir = temp_root / char_name
    temp_dir.mkdir(parents=True, exist_ok=True)

    # Convert mesh — strip embedded animations so they don't duplicate what's
    # in the separate animation FBX files (Unity embeds all clips in the mesh FBX too)
    mesh_glb = temp_dir / (mesh_fbx.stem + ".glb")
    if not convert_one(blender, mesh_fbx, mesh_glb, scale, no_animations=True, flip_y=flip_y):
        return False

    # Convert animations
    anim_glbs: list[Path] = []
    for fbx in anim_fbxes:
        glb = temp_dir / (fbx.stem + ".glb")
        if convert_one(blender, fbx, glb, scale, flip_y=flip_y):
            anim_glbs.append(glb)
        else:
            return False

    # Merge
    if anim_glbs:
        out_glb = out_dir / (char_name + ".glb")
        return merge_character(char_name, mesh_glb, anim_glbs, out_glb)
    else:
        # No animations — just copy the mesh GLB and skip merge
        out_glb = out_dir / (char_name + ".glb")
        out_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(mesh_glb, out_glb)
        print(f"  Copied (no animations): {out_glb.name}")
        return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Batch FBX → GLB converter")
    parser.add_argument("fbx_dir",  help="Source directory containing one subfolder per character")
    parser.add_argument("glb_dir",  help="Output directory for merged GLB + .anim files")
    parser.add_argument("--blender", metavar="PATH", help="Path to blender.exe (auto-detected if omitted)")
    parser.add_argument("--keep-temp", action="store_true", help="Keep per-character temp GLBs after merging")
    parser.add_argument("--only", metavar="NAME", help="Process only this character (folder name)")
    parser.add_argument("--scale", type=float, default=10.0, metavar="FACTOR",
                        help="Uniform scale factor applied to root objects (default: 10)")
    parser.add_argument("--flip-y", action="store_true",
                        help="Rotate all models 180 degrees on the Y axis.")
    args = parser.parse_args()

    blender = find_blender(args.blender)
    if not blender:
        sys.exit(
            "ERROR: Blender not found. Install Blender or pass --blender <path>.\n"
            "  Download: https://www.blender.org/download/"
        )
    print(f"Blender: {blender}")

    fbx_root = Path(args.fbx_dir)
    out_dir  = Path(args.glb_dir)

    if not fbx_root.is_dir():
        sys.exit(f"ERROR: source directory not found: {fbx_root}")

    char_dirs = sorted(d for d in fbx_root.iterdir() if d.is_dir())
    if args.only:
        char_dirs = [d for d in char_dirs if d.name.lower() == args.only.lower()]
    if not char_dirs:
        sys.exit(f"ERROR: no character subdirectories found in {fbx_root}")

    print(f"Found {len(char_dirs)} character(s): {', '.join(d.name for d in char_dirs)}\n")

    with tempfile.TemporaryDirectory(prefix="fbx_convert_") as tmp:
        temp_root = Path(tmp)
        ok = failed = 0
        for char_dir in char_dirs:
            if process_character(char_dir, out_dir, blender, temp_root,
                                 args.scale, args.flip_y):
                ok += 1
            else:
                failed += 1

        if args.keep_temp:
            keep = Path(args.glb_dir) / "_temp_glbs"
            shutil.copytree(tmp, str(keep), dirs_exist_ok=True)
            print(f"\nTemp GLBs kept at: {keep}")

    print(f"\nDone — {ok} succeeded, {failed} failed.")
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
