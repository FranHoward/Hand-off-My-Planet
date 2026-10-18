"""Validate all FBX assets and assembled layouts via Blender's FBX importer."""
from pathlib import Path
import bpy, bmesh, json, math
from mathutils import Vector
OUT=Path(__file__).resolve().parents[1]
manifest=json.loads((OUT/'asset_manifest.json').read_text())
layout=json.loads((OUT/'scene_layout.json').read_text())
results=[]

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01
    raw=path.read_bytes();assert raw.startswith(b'Kaydara FBX Binary'),'Invalid FBX'
    bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False)
    bpy.context.view_layer.update()
    obs=[o for o in scene.objects if o.type=='MESH']
    assert len(obs)==len(scene.objects),'Unexpected non-mesh export'
    return obs,raw

for item in manifest['assets']:
    obs,raw=load(OUT/item['file']);assert len(obs)==1,item['id'];ob=obs[0];me=ob.data
    assert len(me.polygons)==item['triangles'],item['id']
    assert all(len(p.vertices)==3 for p in me.polygons),'Not triangulated'
    bad=[p.index for p in me.polygons if p.area<1e-8]
    assert not bad,(item['id'],'degenerate triangles',bad[:8])
    assert all(math.isfinite(x) for v in me.vertices for x in v.co),'Invalid geometry'
    assert me.uv_layers and all(math.isfinite(x) for lp in me.uv_layers.active.data for x in lp.uv),'Invalid UV'
    assert all(abs(x)<1e-5 for x in ob.location),'Origin not zero'
    assert all(abs(x-1)<1e-5 for x in ob.scale),'Unapplied scale'
    assert all(abs(a-b)<.25 for a,b in zip(ob.dimensions,item['dimensions_cm'])),(item['id'],list(ob.dimensions),item['dimensions_cm'])
    assert [m.name for m in me.materials]==item['materials'],'Material mismatch'
    assert raw.count(b'\x89PNG\r\n\x1a\n')>=4,'Missing embedded maps'
    bm=bmesh.new();bm.from_mesh(me)
    boundary=sum(e.is_boundary for e in bm.edges);bm.free()
    assert boundary==0,(item['id'],'Open edges',boundary)
    r={'file':item['file'],'status':'PASS','triangles':len(me.polygons),
       'dimensions_cm':[round(x,2) for x in ob.dimensions],'boundary_edges':boundary,'embedded_maps':4}
    results.append(r);print('PASS',item['id'],flush=True)

for item in layout['exports']:
    obs,raw=load(OUT/item['file'])
    assert len(obs)==item['objects'],(item['file'],len(obs),item['objects'])
    assert sum(len(o.data.polygons) for o in obs)==item['triangles'],'Layout polygon mismatch'
    # Positions and scales should survive scene export, including reused geometry.
    expected=layout['instances'] if 'FullLayout' in item['file'] else [p for p in layout['instances'] if p['zone']+'_Layout.fbx'==Path(item['file']).name]
    for row in expected:
        ob=bpy.data.objects.get(row['object']);assert ob,('Missing object',row['object'])
        assert (ob.location-Vector(row['location_cm'])).length<.05,('Layout position changed',row['object'])
        assert all(abs(a-b)<1e-5 for a,b in zip(ob.scale,row['scale'])),'Layout scale changed'
    results.append({'file':item['file'],'status':'PASS','objects':len(obs),'triangles':item['triangles'],'placement_transforms':'preserved'})
    print('PASS',item['file'],flush=True)

(OUT/'validation_report.json').write_text(json.dumps({'method':'Blender FBX re-import; no Unreal Engine runtime available',
    'results':results,'asset_count':len(manifest['assets']),'scene_count':len(layout['exports'])},indent=2))
print('VALIDATION_COMPLETE',len(results),flush=True)
