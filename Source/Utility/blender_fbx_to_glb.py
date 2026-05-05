"""
blender_fbx_to_glb.py — Called by Blender headlessly to convert a single FBX to GLB.

Do NOT run directly. batch_convert.py calls this via:
    blender --background --python blender_fbx_to_glb.py -- input.fbx output.glb [--scale 10] [--flip-y]
"""
import argparse
import bpy
import sys


def main() -> None:
    sep = sys.argv.index("--") + 1 if "--" in sys.argv else len(sys.argv)
    inner_args = sys.argv[sep:]

    parser = argparse.ArgumentParser()
    parser.add_argument("input_fbx")
    parser.add_argument("output_glb")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="Uniform scale factor applied to root objects after import.")
    parser.add_argument("--no-animations", action="store_true",
                        help="Export without animations (use for the mesh/skeleton file).")
    parser.add_argument("--flip-y", action="store_true",
                        help="Rotate root objects 180 degrees on the Y axis after import.")
    args = parser.parse_args(inner_args)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(
        filepath=args.input_fbx,
        use_anim=True,
        automatic_bone_orientation=True,
    )

    # The FBX importer adds a coordinate-correction rotation to the root armature object.
    # IrrAssimp ignores the armature root node's rotation for skinned meshes, so rotations
    # must be baked into the bone rest poses via transform_apply rather than left as node
    # transforms. Compose any requested Y flip with the FBX correction before baking so
    # both are applied in a single pass.
    import math, mathutils
    root_objects = [obj for obj in bpy.context.scene.objects if obj.parent is None]

    if args.flip_y:
        flip = mathutils.Matrix.Rotation(math.radians(180), 4, 'Z')
        for obj in root_objects:
            loc, rot, sca = (flip @ obj.matrix_world).decompose()
            obj.location = loc
            obj.rotation_mode = 'QUATERNION'
            obj.rotation_quaternion = rot
            obj.scale = sca
        bpy.context.view_layer.update()

    bpy.ops.object.select_all(action='DESELECT')
    for obj in root_objects:
        obj.select_set(True)
    if root_objects:
        bpy.context.view_layer.objects.active = root_objects[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

    if args.scale != 1.0:
        for obj in root_objects:
            obj.scale = tuple(s * args.scale for s in obj.scale)

    bpy.ops.export_scene.gltf(
        filepath=args.output_glb,
        export_format="GLB",
        export_animations=not args.no_animations,
        export_skins=True,
        export_apply=False,
    )
    print(f"OK  {args.input_fbx}  ->  {args.output_glb}")


main()
