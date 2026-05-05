"""
merge_anims.py — Merge per-clip GLB files from meshy.ai into a single GLB + .anim sidecar.

--- Scan mode (recommended) ---
Copy this script into the folder containing your per-clip GLBs and run it with no arguments:

    python merge_anims.py

It will scan all .glb files in the same directory, merge them alphabetically, and write
<folder_name>.glb + <folder_name>.anim into that directory. The script itself is excluded.

--- Manual mode ---
    python merge_anims.py idle.glb walk.glb run.glb -o character.glb --prefix-strip character_

Dependencies:
    pip install pygltflib numpy

--- How frame ranges are computed ---
IrrAssimp concatenates animations in frame-space using a cumulative frameOffset:
    frame = key.mTime * (targetFPS / ticksPerSecond) + frameOffset
    frameOffset += (mDuration / ticksPerSecond) * targetFPS

For GLTF (timestamps in seconds, ticksPerSecond normalises to 1):
    frame = timestamp_seconds * targetFPS + frameOffset

So each animation's timestamps must stay RELATIVE (starting from 0). This script does
NOT shift timestamps — IrrAssimp handles concatenation. The .anim frame ranges are
computed using the same formula so they stay in sync.
"""

import argparse
import os
import re
import sys

import numpy as np
import pygltflib

# FBX exports from Blender/Maya/Max produce names like "Armature|Take 001|BaseLayer".
# These carry no useful information, so we fall back to the source filename instead.
_GENERIC_NAME_RE = re.compile(r"\||\bTake\s*0*1\b|mixamo\.com", re.IGNORECASE)

TARGET_FPS = 30.0

COMPONENT_COUNT = {
    "SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
    "MAT2": 4, "MAT3": 9, "MAT4": 16,
}

BYTES_PER_COMPONENT = {
    5120: 1,   # BYTE
    5121: 1,   # UNSIGNED_BYTE
    5122: 2,   # SHORT
    5123: 2,   # UNSIGNED_SHORT
    5125: 4,   # UNSIGNED_INT
    5126: 4,   # FLOAT
}


def read_accessor_bytes(gltf: pygltflib.GLTF2, acc_idx: int) -> bytes:
    """
    Return tightly-packed raw bytes for an accessor.
    Handles interleaved bufferViews (byteStride) by de-interleaving.
    """
    acc  = gltf.accessors[acc_idx]
    bv   = gltf.bufferViews[acc.bufferView]
    blob = gltf.binary_blob()

    bpc          = BYTES_PER_COMPONENT.get(acc.componentType, 4)
    n_components = COMPONENT_COUNT.get(acc.type, 1)
    element_size = n_components * bpc
    byte_stride  = bv.byteStride or element_size
    start        = (bv.byteOffset or 0) + (acc.byteOffset or 0)

    if byte_stride == element_size:
        return bytes(blob[start: start + acc.count * element_size])

    # Interleaved — de-interleave into tight packing
    result = bytearray()
    for i in range(acc.count):
        offset = start + i * byte_stride
        result += blob[offset: offset + element_size]
    return bytes(result)


def append_to_blob(
    merged: pygltflib.GLTF2,
    data: bytes,
    component_type: int,
    type_str: str,
    count: int,
    **accessor_kwargs,
) -> int:
    """Append data to the merged GLB binary blob. Returns new accessor index."""
    buf = bytearray(merged.binary_blob() or b"")
    while len(buf) % 4:          # 4-byte alignment (GLTF spec)
        buf += b"\x00"
    byte_offset = len(buf)
    buf += data
    merged.set_binary_blob(bytes(buf))
    merged.buffers[0].byteLength = len(buf)

    bv_idx = len(merged.bufferViews)
    merged.bufferViews.append(pygltflib.BufferView(
        buffer=0, byteOffset=byte_offset, byteLength=len(data),
    ))

    acc_idx = len(merged.accessors)
    merged.accessors.append(pygltflib.Accessor(
        bufferView=bv_idx, componentType=component_type,
        type=type_str, count=count, **accessor_kwargs,
    ))
    return acc_idx


def build_node_name_map(gltf: pygltflib.GLTF2) -> dict[str, int]:
    return {node.name: idx for idx, node in enumerate(gltf.nodes) if node.name}


def remap_node_index(
    src_idx: int,
    src: pygltflib.GLTF2,
    base_name_map: dict[str, int],
    src_path: str,
) -> int:
    if src_idx is None or src_idx >= len(src.nodes):
        return src_idx
    node_name = src.nodes[src_idx].name
    if node_name and node_name in base_name_map:
        return base_name_map[node_name]
    print(
        f"  WARNING: node '{node_name}' (idx {src_idx}) in "
        f"'{os.path.basename(src_path)}' not found in base by name — using raw index.",
        file=sys.stderr,
    )
    return src_idx


def merge_one_animation(
    merged: pygltflib.GLTF2,
    src: pygltflib.GLTF2,
    anim: pygltflib.Animation,
    base_name_map: dict[str, int],
    src_path: str,
) -> tuple[pygltflib.Animation, float]:
    """
    Copy one animation from src into merged.

    Timestamps are NOT shifted — they stay relative to 0 exactly as in the source file.
    IrrAssimp's createAnimation() applies its own cumulative frameOffset when loading,
    which is what sequences the clips in frame-space. Pre-shifting would double-count
    that offset and produce completely wrong key positions.

    Returns (new Animation, clip_duration_seconds).
    """
    new_samplers: list = []
    sampler_map: dict[int, int] = {}
    clip_duration_s = 0.0

    for s_idx, sampler in enumerate(anim.samplers):
        # Timestamps — read as-is, no shift
        ts_raw = read_accessor_bytes(src, sampler.input)
        ts     = np.frombuffer(ts_raw, dtype=np.float32)
        if len(ts):
            clip_duration_s = max(clip_duration_s, float(ts[-1]))

        new_in = append_to_blob(
            merged, ts_raw,
            pygltflib.FLOAT, "SCALAR", len(ts),
            min=[float(ts.min())] if len(ts) else [0.0],
            max=[float(ts.max())] if len(ts) else [0.0],
        )

        # Values — pass through unchanged (tightly packed by read_accessor_bytes)
        src_acc = src.accessors[sampler.output]
        val_raw = read_accessor_bytes(src, sampler.output)
        new_out = append_to_blob(
            merged, val_raw,
            src_acc.componentType, src_acc.type, src_acc.count,
        )

        sampler_map[s_idx] = len(new_samplers)
        new_samplers.append(pygltflib.AnimationSampler(
            input=new_in, output=new_out, interpolation=sampler.interpolation,
        ))

    # Remap channel bone indices from source file → base file via name
    new_channels = [
        pygltflib.AnimationChannel(
            sampler=sampler_map[ch.sampler],
            target=pygltflib.AnimationChannelTarget(
                node=remap_node_index(ch.target.node, src, base_name_map, src_path),
                path=ch.target.path,
            ),
        )
        for ch in anim.channels
    ]

    return pygltflib.Animation(samplers=new_samplers, channels=new_channels), clip_duration_s


def write_anim_sidecar(
    output_path: str,
    clip_ranges: list[tuple[str, int, int]],
) -> None:
    """
    Write the engine's XML .anim sidecar.
    clip_ranges contains (name, start_frame, end_frame) integers matching
    IrrAssimp's frame calculation exactly.
    """
    anim_path = os.path.splitext(output_path)[0] + ".anim"
    with open(anim_path, "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<animation>\n')
        f.write(f"\t<fps>{int(TARGET_FPS)}</fps>\n")
        f.write(f'\t<animationList size="dynamic">\n')
        for i, (name, sf, ef) in enumerate(clip_ranges):
            f.write(f'\t\t<a{i}  name="{name}" loop="0">{sf},{ef}</a{i}>\n')
        f.write("\t</animationList>\n</animation>\n")
    print(f"Wrote {anim_path}")
    print()
    print("NOTE: All clips default to loop=\"1\".")
    print("      Edit the .anim to set loop=\"0\" for one-shot clips (die, attack, etc.).")


def scan_glbs(script_path: str) -> tuple[list[str], str]:
    directory   = os.path.dirname(os.path.abspath(script_path))
    folder_name = os.path.basename(directory)
    output_path = os.path.join(directory, folder_name + ".glb")
    glb_files   = sorted(
        os.path.join(directory, f)
        for f in os.listdir(directory)
        if f.lower().endswith(".glb")
        and os.path.join(directory, f) != output_path
    )
    return glb_files, output_path


def common_prefix(names: list[str]) -> str:
    if not names:
        return ""
    prefix = names[0]
    for name in names[1:]:
        while not name.startswith(prefix):
            prefix = prefix[:-1]
            if not prefix:
                return ""
    return prefix


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Merge per-clip GLBs from meshy.ai into a single GLB. "
                    "Run with no arguments to auto-scan the script's directory."
    )
    parser.add_argument("inputs", nargs="*",
                        help="Input GLB files. Omit to scan the script's directory.")
    parser.add_argument("-o", "--output", default="",
                        help="Output GLB path. Auto-derived from directory name in scan mode.")
    parser.add_argument("--prefix-strip", default="", metavar="PREFIX",
                        help="Strip prefix from filenames to derive clip names.")
    parser.add_argument("--no-anim", action="store_true",
                        help="Skip writing the .anim sidecar.")
    parser.add_argument("--use-filenames", action="store_true",
                        help="Always derive clip names from filenames, ignoring embedded names.")
    args = parser.parse_args()

    # Scan mode
    if not args.inputs:
        args.inputs, derived_output = scan_glbs(__file__)
        if not args.inputs:
            print("ERROR: No .glb files found in the script's directory.", file=sys.stderr)
            sys.exit(1)
        if not args.output:
            args.output = derived_output
        if not args.prefix_strip:
            stems = [os.path.splitext(os.path.basename(p))[0] for p in args.inputs]
            args.prefix_strip = common_prefix(stems)
        print(f"Scan mode: {len(args.inputs)} file(s) in '{os.path.dirname(args.output)}'")
        print(f"  Output : {args.output}")
        print(f"  Clip name prefix stripped: '{args.prefix_strip}'")
        print()
    else:
        if not args.output:
            parser.error("-o/--output is required in manual mode.")

    print(f"Loading {len(args.inputs)} source file(s)...")
    sources: list[pygltflib.GLTF2] = []
    for path in args.inputs:
        if not os.path.isfile(path):
            print(f"ERROR: File not found: {path}", file=sys.stderr)
            sys.exit(1)
        sources.append(pygltflib.GLTF2().load(path))
        print(f"  {os.path.basename(path)} — {len(sources[-1].animations)} animation(s)")

    # Warn if node topology differs across files
    base_node_names = [n.name for n in sources[0].nodes]
    for src, path in zip(sources[1:], args.inputs[1:]):
        if [n.name for n in src.nodes] != base_node_names:
            print(f"  WARNING: node list in '{os.path.basename(path)}' differs from base — "
                  "remapping by name.", file=sys.stderr)
            break

    print(f"\nBuilding merged GLB from base: {os.path.basename(args.inputs[0])}")
    merged = pygltflib.GLTF2().load(args.inputs[0])
    merged.animations = []

    base_name_map  = build_node_name_map(merged)

    # frame_cursor mirrors IrrAssimp's frameOffset accumulator exactly:
    #   frameOffset += (mDuration / ticksPerSecond) * targetFPS
    # For GLTF (timestamps in seconds): frameOffset += dur_seconds * TARGET_FPS
    frame_cursor   = 0.0
    clip_ranges: list[tuple[str, int, int]] = []

    for src, path in zip(sources, args.inputs):
        seen_in_file: set[str] = set()
        for anim in src.animations:
            embedded = anim.name or ""
            use_embedded = embedded and not args.use_filenames and not _GENERIC_NAME_RE.search(embedded)
            clip_name = embedded if use_embedded else clip_name_from_file(path, args.prefix_strip)

            if clip_name in seen_in_file:
                print(f"  SKIP duplicate '{clip_name}' in {os.path.basename(path)}")
                continue
            seen_in_file.add(clip_name)

            new_anim, dur_s = merge_one_animation(
                merged, src, anim, base_name_map, path
            )
            new_anim.name = clip_name
            merged.animations.append(new_anim)

            # +1 skips the boundary frame shared with the previous animation's
            # last keyframe, which IrrAssimp stores at the same position.
            start_f = int(frame_cursor) + (1 if clip_ranges else 0)
            end_f   = int(frame_cursor + dur_s * TARGET_FPS)
            clip_ranges.append((clip_name, start_f, end_f))
            print(f"  [{clip_name}]  frames {start_f}–{end_f}  ({dur_s:.3f}s)")

            frame_cursor += dur_s * TARGET_FPS   # no gap — matches IrrAssimp exactly

    merged.save(args.output)
    print(f"\nWrote {os.path.basename(args.output)}  ({len(clip_ranges)} animation(s))")

    if not args.no_anim:
        write_anim_sidecar(args.output, clip_ranges)


def clip_name_from_file(path: str, prefix_strip: str = "") -> str:
    name = os.path.splitext(os.path.basename(path))[0]
    if prefix_strip and name.startswith(prefix_strip):
        name = name[len(prefix_strip):]
    return name


if __name__ == "__main__":
    main()
