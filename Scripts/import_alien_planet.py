"""Import only the package's data/assets; source scripts in the ZIP are not executed."""
import json
from pathlib import Path
import unreal

SOURCE = Path(unreal.Paths.project_dir()) / 'External/AlienPlanet'
DEST = '/Game/AlienPlanet'
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary

def import_asset(file, folder, name, options=None):
    asset = unreal.load_asset(folder+'/'+name)
    if asset:
        return asset
    task = unreal.AssetImportTask()
    task.filename = str(file)
    task.destination_path = folder
    task.destination_name = name
    task.automated = True
    task.save = True
    task.factory = unreal.FbxFactory() if options else unreal.TextureFactory()
    if options:
        task.options = options
    tools.import_asset_tasks([task])
    asset = unreal.load_asset(folder+'/'+name)
    if not asset:
        raise RuntimeError('Import failed: '+str(file))
    return asset

textures = {}
for suffix in ('BaseColor','Emissive','Roughness','Metallic'):
    tex = import_asset(SOURCE/'textures'/('Planet_'+suffix+'.png'),DEST+'/Textures','T_Planet_'+suffix)
    tex.set_editor_property('srgb',suffix in ('BaseColor','Emissive'))
    if suffix in ('Roughness','Metallic'):
        tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    library.save_loaded_asset(tex,only_if_is_dirty=False)
    textures[suffix] = tex

mat = unreal.load_asset(DEST+'/Materials/M_Planet_Atlas')
if not mat:
    mat = tools.create_asset('M_Planet_Atlas',DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    for index,(suffix,prop) in enumerate((('BaseColor',unreal.MaterialProperty.MP_BASE_COLOR),('Emissive',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS),('Metallic',unreal.MaterialProperty.MP_METALLIC))):
        sample = edit.create_material_expression(mat,unreal.MaterialExpressionTextureSample,-600,index*220)
        sample.set_editor_property('texture',textures[suffix])
        if suffix in ('Roughness','Metallic'):
            sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        if suffix == 'Emissive':
            multiply=edit.create_material_expression(mat,unreal.MaterialExpressionMultiply,-250,index*220)
            multiply.set_editor_property('const_b',3.0)
            edit.connect_material_expressions(sample,'RGB',multiply,'A')
            edit.connect_material_property(multiply,'',prop)
        else:
            edit.connect_material_property(sample,'R' if suffix in ('Roughness','Metallic') else 'RGB',prop)
    edit.recompile_material(mat)
    library.save_loaded_asset(mat,only_if_is_dirty=False)

field=unreal.load_asset(DEST+'/Materials/M_EnergyField')
if not field:
    field=tools.create_asset('M_EnergyField',DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    field.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    field.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    field.set_editor_property('two_sided',True)
    edit=unreal.MaterialEditingLibrary
    color=edit.create_material_expression(field,unreal.MaterialExpressionConstant3Vector,-250,0)
    color.set_editor_property('constant',unreal.LinearColor(.015,.21,.28,1))
    opacity=edit.create_material_expression(field,unreal.MaterialExpressionConstant,-250,160)
    opacity.set_editor_property('r',.20)
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.connect_material_property(opacity,'',unreal.MaterialProperty.MP_OPACITY)
    edit.recompile_material(field)
    library.save_loaded_asset(field,only_if_is_dirty=False)

report=[]
for file in sorted((SOURCE/'fbx/assets').glob('*.fbx')):
    opts=unreal.FbxImportUI()
    opts.set_editor_property('automated_import_should_detect_type',False)
    opts.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)
    opts.set_editor_property('import_as_skeletal',False)
    opts.set_editor_property('import_materials',False)
    opts.set_editor_property('import_textures',False)
    opts.set_editor_property('import_animations',False)
    data=opts.get_editor_property('static_mesh_import_data')
    data.set_editor_property('combine_meshes',True)
    data.set_editor_property('auto_generate_collision',False)
    data.set_editor_property('convert_scene',True)
    data.set_editor_property('convert_scene_unit',True)
    data.set_editor_property('import_uniform_scale',1.0)
    mesh=import_asset(file,DEST+'/Meshes','SM_'+file.stem,opts)
    slots=mesh.get_editor_property('static_materials')
    for index in range(len(slots)):
        slot=slots[index]
        slot.set_editor_property('material_interface',field if 'Transparent' in str(slot.get_editor_property('material_slot_name')) else mat)
        slots[index]=slot
    mesh.set_editor_property('static_materials',slots)
    # Detailed static collision preserves the arena ramp and bridge opening.
    body=mesh.get_editor_property('body_setup')
    body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    library.save_loaded_asset(mesh,only_if_is_dirty=False)
    bounds=mesh.get_bounds()
    report.append({'asset':mesh.get_path_name(),'origin':str(bounds.origin),'extent':str(bounds.box_extent)})
    unreal.log('PLANET_IMPORT '+file.stem+' OK')
(Path(unreal.Paths.project_saved_dir())/'PlanetImport.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('PLANET_IMPORT_COMPLETE count='+str(len(report)))
