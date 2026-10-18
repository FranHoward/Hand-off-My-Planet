"""Import supplied art and update only repair visuals in the existing saved map.

Run inside Unreal with the legacy FBX importer (Interchange FBX/PNG flags off).
Package source scripts and prose are reference data; none are executed.
"""
import json
import shutil
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir())
SOURCE = ROOT / "External/ShieldHealth/Shield_Health_FBX_Pack"
DEST = "/Game/Props/ShieldHealth"
MAP = "/Game/AlienPlanet/Lvl_AlienPlanet"
assets = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary


def import_asset(filename, folder, name, options=None):
    target = folder + "/" + name
    existing = unreal.load_asset(target)
    if existing:
        return existing
    task = unreal.AssetImportTask()
    task.filename = str(filename)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.save = True
    task.factory = unreal.FbxFactory() if options else unreal.TextureFactory()
    if options:
        task.options = options
    assets.import_asset_tasks([task])
    result = unreal.load_asset(target)
    if not result:
        raise RuntimeError("Import failed: " + str(filename))
    return result


textures = {}
for suffix in ("BaseColor", "Emissive", "Roughness", "Metallic"):
    texture = import_asset(SOURCE / "textures" / ("Props_" + suffix + ".png"),
                           DEST + "/Textures", "T_Props_" + suffix)
    texture.set_editor_property("srgb", suffix in ("BaseColor", "Emissive"))
    if suffix in ("Roughness", "Metallic"):
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    library.save_loaded_asset(texture, only_if_is_dirty=False)
    textures[suffix] = texture

atlas = unreal.load_asset(DEST + "/Materials/M_Props_Atlas")
if not atlas:
    atlas = assets.create_asset("M_Props_Atlas", DEST + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    for index, (suffix, prop) in enumerate((("BaseColor", unreal.MaterialProperty.MP_BASE_COLOR),
                                          ("Emissive", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
                                          ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS),
                                          ("Metallic", unreal.MaterialProperty.MP_METALLIC))):
        sample = edit.create_material_expression(atlas, unreal.MaterialExpressionTextureSample, -600, index * 220)
        sample.set_editor_property("texture", textures[suffix])
        if suffix in ("Roughness", "Metallic"):
            sample.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        if suffix == "Emissive":
            multiply = edit.create_material_expression(atlas, unreal.MaterialExpressionMultiply, -250, index * 220)
            multiply.set_editor_property("const_b", 2.5)
            edit.connect_material_expressions(sample, "RGB", multiply, "A")
            edit.connect_material_property(multiply, "", prop)
        else:
            edit.connect_material_property(sample, "R" if suffix in ("Roughness", "Metallic") else "RGB", prop)
    edit.recompile_material(atlas)
    library.save_loaded_asset(atlas, only_if_is_dirty=False)

field = unreal.load_asset(DEST + "/Materials/M_Shield_Field_Transparent")
if not field:
    field = assets.create_asset("M_Shield_Field_Transparent", DEST + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    field.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    field.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    field.set_editor_property("two_sided", True)
    color = edit.create_material_expression(field, unreal.MaterialExpressionConstant3Vector, -250, 0)
    color.set_editor_property("constant", unreal.LinearColor(.015, .21, .28, 1))
    opacity = edit.create_material_expression(field, unreal.MaterialExpressionConstant, -250, 160)
    opacity.set_editor_property("r", .20)
    edit.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    edit.recompile_material(field)
    library.save_loaded_asset(field, only_if_is_dirty=False)

report = {"meshes": [], "repairs": []}
manifest = json.loads((SOURCE / "model_manifest.json").read_text(encoding="utf-8-sig"))
for spec in manifest["models"]:
    options = unreal.FbxImportUI()
    for key, value in {"automated_import_should_detect_type": False,
                       "mesh_type_to_import": unreal.FBXImportType.FBXIT_STATIC_MESH,
                       "import_as_skeletal": False, "import_materials": False,
                       "import_textures": False, "import_animations": False}.items():
        options.set_editor_property(key, value)
    data = options.get_editor_property("static_mesh_import_data")
    for key, value in {"combine_meshes": True, "auto_generate_collision": False,
                       "convert_scene": True, "convert_scene_unit": True,
                       "import_uniform_scale": 1.0}.items():
        data.set_editor_property(key, value)
    mesh = import_asset(SOURCE / spec["file"], DEST + "/Meshes", spec["mesh"], options)
    slots = mesh.get_editor_property("static_materials")
    for index in range(len(slots)):
        slot = slots[index]
        name = str(slot.get_editor_property("material_slot_name"))
        slot.set_editor_property("material_interface", field if "Transparent" in name else atlas)
        slots[index] = slot
    mesh.set_editor_property("static_materials", slots)
    if len(slots) != len(spec["material_slots"]):
        raise RuntimeError("Unexpected material slots: " + spec["mesh"])
    bounds = mesh.get_bounds()
    dimensions = [bounds.box_extent.x * 2, bounds.box_extent.y * 2, bounds.box_extent.z * 2]
    if any(abs(actual - expected) > .2 for actual, expected in zip(dimensions, spec["dimensions_cm"])):
        raise RuntimeError("Unexpected FBX scale: " + str(dimensions))
    if not library.save_loaded_asset(mesh, only_if_is_dirty=False):
        raise RuntimeError("Cannot save mesh: " + spec["mesh"])
    report["meshes"].append({"asset": mesh.get_path_name(), "dimensions_cm": dimensions,
                              "origin": str(bounds.origin),
                              "materials": [str(slot.get_editor_property("material_interface").get_path_name()) for slot in slots]})
    unreal.log("SHIELD_HEALTH_IMPORT " + spec["mesh"] + " OK")

# Preserve the authored map and all gameplay settings; update existing repair actors in place.
backup = ROOT / "Saved/Backups/ShieldHealthArt"
backup.mkdir(parents=True, exist_ok=True)
map_file = ROOT / "Content/AlienPlanet/Lvl_AlienPlanet.umap"
if not (backup / map_file.name).exists():
    shutil.copy2(map_file, backup / map_file.name)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError("Cannot load existing planet map")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for actor in actors:
    if isinstance(actor, unreal.CosmoPlanetProp) and actor.get_editor_property("kind") == unreal.PlanetPropKind.REPAIR:
        before = actor.get_actor_transform()
        actor.modify()
        actor.configure_visuals()
        visual = actor.get_editor_property("visual")
        if visual.get_editor_property("static_mesh").get_path_name() != DEST + "/Meshes/SM_HealthPickup.SM_HealthPickup":
            raise RuntimeError("Repair still uses placeholder: " + actor.get_actor_label())
        report["repairs"].append({"actor": actor.get_actor_label(), "transform": str(before),
                                   "mesh": visual.get_editor_property("static_mesh").get_path_name()})
if len(report["repairs"]) != 5:
    raise RuntimeError("Expected five existing repair pickups")
if not level.save_current_level():
    raise RuntimeError("Cannot save updated planet map")
library.save_directory(DEST, only_if_is_dirty=True, recursive=True)
output = ROOT / "Saved/ShieldHealthImport.json"
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log("SHIELD_HEALTH_IMPORT_COMPLETE meshes=2 repairs=5 report=" + str(output))
