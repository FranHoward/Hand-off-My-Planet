"""Re-import both exported FBX files; verify geometry, units, pivots and materials."""
from pathlib import Path
import bpy, json, math, bmesh

OUT=Path(__file__).resolve().parents[1]
manifest=json.loads((OUT/'model_manifest.json').read_text())
results=[]
for item in manifest['models']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01
    path=OUT/item['file'];raw=path.read_bytes()
    assert raw.startswith(b'Kaydara FBX Binary'), 'Not a binary FBX'
    bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False)
    meshes=[o for o in scene.objects if o.type=='MESH']
    assert len(meshes)==1 and len(scene.objects)==1,'Unexpected export objects'
    ob=meshes[0];me=ob.data
    assert len(me.polygons)==item['triangles'], 'Triangle count changed'
    assert all(len(f.vertices)==3 for f in me.polygons),'Non-triangle polygon'
    assert all(math.isfinite(x) for v in me.vertices for x in v.co),'Nonfinite coordinate'
    assert all(f.area>1e-8 for f in me.polygons),'Degenerate triangle'
    assert me.uv_layers,'Missing UVs'
    assert all(math.isfinite(x) for lp in me.uv_layers.active.data for x in lp.uv),'Invalid UV'
    assert list(ob.location)==[0,0,0],'Pivot not at world origin'
    assert all(abs(x-1)<1e-6 for x in ob.scale),'Scale not applied'
    bpy.context.view_layer.update();dims=list(ob.dimensions)
    assert all(abs(a-b)<.15 for a,b in zip(dims,item['dimensions_cm'])), (dims,item['dimensions_cm'])
    assert [m.name for m in me.materials]==item['material_slots'],'Material slots changed'
    images=[]
    for mat in me.materials:
        if mat.use_nodes:
            images += [n.image for n in mat.node_tree.nodes if n.type=='TEX_IMAGE' and n.image]
    assert images and all(im.size[0]>0 for im in images),'Missing textures'
    assert raw.count(b'\x89PNG\r\n\x1a\n')>=4,'Expected four embedded PNGs'
    bm=bmesh.new();bm.from_mesh(me)
    boundary=sum(1 for edge in bm.edges if edge.is_boundary)
    assert boundary==0, 'Unclosed geometry'
    bm.free()
    result={'asset':path.name,'status':'PASS','vertices':len(me.vertices),'triangles':len(me.polygons),
            'dimensions_cm':[round(x,2) for x in dims],'materials':[m.name for m in me.materials],
            'uv':'present','boundary_edges':boundary,'pivot_cm':list(ob.location),
            'embedded_png_count':raw.count(b'\x89PNG\r\n\x1a\n'),
            'notes':'Validated by Blender FBX re-import. Unreal Engine is not available for an in-engine test.'}
    results.append(result);print(json.dumps(result),flush=True)
(OUT/'validation_report.json').write_text(json.dumps(results,indent=2))
