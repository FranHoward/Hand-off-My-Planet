"""Import the supplied creature FBXs with one skeleton per species; run inside UE."""
import json
from pathlib import Path
import unreal

SOURCE = Path(unreal.Paths.project_dir()) / "External/CreatureAnimations"
DEST = "/Game/Creatures"
REPORT = {"species": {}}
assets = unreal.AssetToolsHelpers.get_asset_tools()


def run_import(filename, folder, name, options=None, replace=False):
    target = folder + "/" + name
    existing = unreal.load_asset(target)
    if existing and not replace:
        return existing
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.replace_existing = replace
    task.save = True
    if options:
        task.options = options
        task.factory = unreal.FbxFactory()
    else:
        task.factory = unreal.TextureFactory()
    assets.import_asset_tasks([task])
    result = unreal.load_asset(target)
    if not result:
        paths = task.get_editor_property("imported_object_paths")
        desired_class = unreal.SkeletalMesh if name.startswith("SK_") else unreal.AnimSequence if name.startswith("AS_") else unreal.Texture
        matches = [unreal.load_asset(p) for p in paths]
        matches = [a for a in matches if isinstance(a, desired_class)]
        if len(matches) != 1:
            raise RuntimeError(f"Import did not yield {name}: {paths}")
        if not unreal.EditorAssetLibrary.rename_asset(matches[0].get_path_name(), target):
            raise RuntimeError(f"Cannot rename imported asset to {target}")
        result = unreal.load_asset(target)
    if not result:
        raise RuntimeError(f"Import failed: {filename}")
    return result


def options_for(mesh=True, skeleton=None):
    opts = unreal.FbxImportUI()
    opts.set_editor_property("automated_import_should_detect_type", False)
    opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH if mesh else unreal.FBXImportType.FBXIT_ANIMATION)
    opts.set_editor_property("import_as_skeletal", True)
    opts.set_editor_property("import_mesh", mesh)
    opts.set_editor_property("import_animations", not mesh)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("create_physics_asset", False)
    if skeleton:
        opts.set_editor_property("skeleton", skeleton)
    data = opts.get_editor_property("skeletal_mesh_import_data" if mesh else "anim_sequence_import_data")
    for key, value in {"convert_scene": True, "convert_scene_unit": True, "import_uniform_scale": 1.0}.items():
        data.set_editor_property(key, value)
    if mesh:
        data.set_editor_property("use_t0_as_ref_pose", False)
        data.set_editor_property("update_skeleton_reference_pose", False)
    else:
        data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property("use_default_sample_rate", True)
        data.set_editor_property("custom_sample_rate", 30)
    return opts


def make_material(kind, folder):
    textures = {}
    for suffix in ("BaseColor", "Emissive", "Roughness"):
        filename = next((SOURCE / kind / "Textures").glob("*_" + suffix + ".png"))
        texture = run_import(filename, folder + "/Textures", f"T_{kind}_{suffix}")
        texture.set_editor_property("srgb", suffix != "Roughness")
        if suffix == "Roughness":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)
        textures[suffix] = texture
    name = "M_" + kind
    mat = unreal.load_asset(folder + "/Materials/" + name)
    if mat:
        mat.set_editor_property("used_with_skeletal_mesh", True)
        unreal.MaterialEditingLibrary.recompile_material(mat)
        unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
        return mat
    mat = assets.create_asset(name, folder + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("used_with_skeletal_mesh", True)
    lib = unreal.MaterialEditingLibrary
    for index, (suffix, prop) in enumerate((("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
                                           ("Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                           ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS))):
        sample = lib.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -600, index * 240)
        sample.set_editor_property("texture", textures[suffix])
        if suffix == "Roughness":
            sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        if suffix == "Emissive":
            multiply = lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, index * 240)
            multiply.set_editor_property("const_b", 2.0)
            lib.connect_material_expressions(sample, "RGB", multiply, "A")
            lib.connect_material_property(multiply, "", prop)
        else:
            lib.connect_material_property(sample, "R" if suffix == "Roughness" else "RGB", prop)
    metal = lib.create_material_expression(mat, unreal.MaterialExpressionConstant, -250, 720)
    metal.set_editor_property("r", .18)
    lib.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    lib.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


for kind in ("CrystalEye", "Minion", "Boss"):
    folder = DEST + "/" + kind
    mesh = run_import(SOURCE / kind / f"{kind}_SkeletalMesh.fbx", folder + "/Mesh", "SK_" + kind, options_for())
    skeleton = mesh.get_editor_property("skeleton")
    if not skeleton:
        skeleton = unreal.load_asset(folder + "/Mesh/SKEL_" + kind) or unreal.load_asset(folder + "/Mesh/SK_" + kind + "_Skeleton")
        if skeleton:
            mesh = run_import(SOURCE / kind / f"{kind}_SkeletalMesh.fbx", folder + "/Mesh", "SK_" + kind,
                              options_for(True, skeleton), replace=True)
    if not skeleton:
        raise RuntimeError(f"Missing skeleton: {kind}")
    # Keep the importer-created SK_<species>_Skeleton name; no cross-skeleton retargeting.
    unreal.EditorAssetLibrary.save_loaded_asset(skeleton)
    material = make_material(kind, folder)
    materials = mesh.get_editor_property("materials")
    for index in range(len(materials)):
        slot = materials[index]
        slot.set_editor_property("material_interface", material)
        # Array elements are struct copies in the Unreal Python API.
        materials[index] = slot
    mesh.set_editor_property("materials", materials)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError("Cannot save mesh/material binding: " + kind)
    unreal.log("CREATURE_MATERIAL_BOUND " + kind + " " + str(mesh.get_editor_property("materials")))
    entry = {"mesh": mesh.get_path_name(), "skeleton": skeleton.get_path_name(), "animations": []}
    for spec in json.loads((SOURCE / kind / "events.json").read_text(encoding="utf-8-sig")):
        seq = run_import(SOURCE / kind / (spec["name"] + ".fbx"), folder + "/Animations", "AS_" + spec["name"], options_for(False, skeleton))
        seq.set_editor_property("enable_root_motion", False)
        seq.set_editor_property("force_root_lock", False)
        if seq.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Skeleton mismatch: " + spec["name"])
        length = seq.get_play_length()
        if abs(length - spec["duration_seconds"]) > .001:
            raise RuntimeError(f"Duration mismatch {spec['name']}: {length}")
        unreal.EditorAssetLibrary.save_loaded_asset(seq)
        entry["animations"].append({"asset": seq.get_path_name(), "duration": length, "loop": spec["loop"]})
    REPORT["species"][kind] = entry
    unreal.EditorAssetLibrary.save_directory(folder, only_if_is_dirty=True, recursive=True)
    unreal.log("CREATURE_IMPORT " + kind + " OK")

output = Path(unreal.Paths.project_saved_dir()) / "CreatureAnimationImport.json"
output.write_text(json.dumps(REPORT, indent=2), encoding="utf-8")
unreal.log("CREATURE_IMPORT_COMPLETE " + str(output))
