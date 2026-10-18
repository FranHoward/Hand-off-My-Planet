"""Original stylized game props: energy shield + health pickup.
Run with Blender 4.5 Python: blender --background --python build_props.py
Or run in a Python 3.11 environment with bpy==4.5.14, numpy and Pillow.
Geometry authored in meters, converted to centimeter coordinates. Front = -Y.
Only the two static meshes are exported. Studio objects are preview-only.
"""
import bpy, bmesh, math, json, os
from pathlib import Path
from mathutils import Vector
import numpy as np
from PIL import Image

OUT=Path(os.environ.get('PROPS_OUTPUT',Path(__file__).resolve().parents[1]))
for p in ['textures','preview']: (OUT/p).mkdir(parents=True,exist_ok=True)
U=100.0
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01

# One shared padded atlas. Field opacity is a separate named material slot.
COLORS=[(23,35,49),(58,80,98),(160,187,195),(216,232,226),
        (13,112,149),(33,205,245),(148,244,255),(6,15,25),
        (128,27,24),(224,60,30),(255,113,47),(255,217,159),
        (41,51,68),(89,106,120),(255,248,220),(24,72,92)]
EMIT={4:.55,5:1,6:1,9:.3,10:1,11:1,14:.7}
W=1024;T=256
base=np.zeros((W,W,3),np.uint8);emiss=np.zeros_like(base)
rough=np.zeros((W,W),np.uint8);metal=np.zeros((W,W),np.uint8)
rng=np.random.default_rng(13133);yy,xx=np.mgrid[0:T,0:T]/T
for i,col in enumerate(COLORS):
    r,c=divmod(i,4);s=np.s_[r*T:(r+1)*T,c*T:(c+1)*T]
    grain=rng.normal(0,.013,(T,T));shade=.97+grain+.018*np.sin(xx*140)
    if i in (0,1,2,3,12,13): shade-=.025*(np.sin(yy*133+xx*3)>.97)
    base[s]=np.clip(np.array(col)[None,None,:]*shade[:,:,None],0,255).astype(np.uint8)
    if i in EMIT: emiss[s]=np.clip(np.array(col)*EMIT[i],0,255).astype(np.uint8)
    rough[s]=int((.27 if i in EMIT else .40)*255)
    metal[s]=int((.65 if i in (0,1,2,12,13) else .08)*255)
for suffix,data in [('BaseColor',base),('Emissive',emiss),('Roughness',rough),('Metallic',metal)]:
    Image.fromarray(data).save(OUT/'textures'/('Props_'+suffix+'.png'))

atlas=bpy.data.materials.new('M_Props_Atlas');atlas.use_nodes=True
bs=atlas.node_tree.nodes.get('Principled BSDF');bs.inputs['Emission Strength'].default_value=2.2
for index,(s,inp) in enumerate([('BaseColor','Base Color'),('Emissive','Emission Color'),('Roughness','Roughness'),('Metallic','Metallic')]):
    node=atlas.node_tree.nodes.new('ShaderNodeTexImage')
    node.image=bpy.data.images.load(str(OUT/'textures'/('Props_'+s+'.png')))
    node.location=(-600,300-index*230);node.label=s
    if s in ('Roughness','Metallic'): node.image.colorspace_settings.name='Non-Color'
    atlas.node_tree.links.new(node.outputs['Color'],bs.inputs[inp])
atlas.diffuse_color=(.08,.30,.40,1)
field=bpy.data.materials.new('M_Shield_Field_Transparent');field.use_nodes=True
p=field.node_tree.nodes.get('Principled BSDF')
p.inputs['Base Color'].default_value=(.015,.21,.28,1)
p.inputs['Metallic'].default_value=.1;p.inputs['Roughness'].default_value=.3
p.inputs['Alpha'].default_value=.20
p.inputs['Emission Color'].default_value=(.01,.24,.32,1)
p.inputs['Emission Strength'].default_value=.4
field.diffuse_color=(.015,.21,.28,.20)
field['unreal_blend_mode']='Translucent';field['unreal_opacity']=.20

def select(obs):
    bpy.ops.object.select_all(action='DESELECT')
    for o in obs:o.select_set(True)
    bpy.context.view_layer.objects.active=obs[0] if obs else None

def surface(o,slot,smooth=False,material=None):
    o.data.materials.clear();o.data.materials.append(material or atlas)
    if not o.data.uv_layers:o.data.uv_layers.new(name='UVMap')
    uv=o.data.uv_layers.active.data;r,c=divmod(slot,4)
    for f in o.data.polygons:
        f.use_smooth=smooth
        axes=[0,1,2];axes.remove(max(range(3),key=lambda j:abs(f.normal[j])))
        vv=[o.data.vertices[o.data.loops[k].vertex_index].co for k in f.loop_indices]
        lo=[min(v[a] for v in vv) for a in axes]
        span=[max(v[a] for v in vv)-lo[j] for j,a in enumerate(axes)]
        for k,v in zip(f.loop_indices,vv):
            x=(v[axes[0]]-lo[0])/max(span[0],1e-8)
            z=(v[axes[1]]-lo[1])/max(span[1],1e-8)
            uv[k].uv=((c+.08+.84*x)/4,(3-r+.08+.84*z)/4)
    return o

def mesh(name,verts,faces,slot,smooth=False,material=None):
    me=bpy.data.meshes.new(name);me.from_pydata([Vector(v)*U for v in verts],[],faces);me.update()
    bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
    o=bpy.data.objects.new(name,me);scene.collection.objects.link(o)
    return surface(o,slot,smooth,material)

def box(name,center,size,slot,bevel=.015,rotation=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=Vector(center)*U)
    o=bpy.context.object;o.name=name
    o.dimensions=Vector(size)*U;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        mod=o.modifiers.new('Manufactured bevel','BEVEL');mod.width=min(bevel,min(size)*.4)*U;mod.segments=2
        bpy.ops.object.modifier_apply(modifier=mod.name)
    surface(o,slot);o.rotation_euler.z=rotation;return o

def tube(name,points,radius,slot,sides=8,smooth=True):
    pts=[Vector(p) for p in points];vv=[]
    for i,p in enumerate(pts):
        d=(pts[min(i+1,len(pts)-1)]-pts[max(0,i-1)]).normalized()
        up=Vector((0,0,1))
        if abs(d.dot(up))>.92:up=Vector((0,1,0))
        a=d.cross(up).normalized();b=d.cross(a).normalized()
        for j in range(sides):
            t=j*math.tau/sides;vv.append(p+radius*(a*math.cos(t)+b*math.sin(t)))
    ff=[tuple(reversed(range(sides)))]
    for i in range(len(pts)-1):
        for j in range(sides):
            k=(j+1)%sides;ff.append((i*sides+j,i*sides+k,(i+1)*sides+k,(i+1)*sides+j))
    ff.append(tuple((len(pts)-1)*sides+j for j in range(sides)))
    return mesh(name,vv,ff,slot,smooth)

def cylinder(name,center,radius,depth,slot,sides=32,axis='Z',bevel=.005):
    bpy.ops.mesh.primitive_cylinder_add(vertices=sides,radius=radius*U,depth=depth*U,location=Vector(center)*U)
    o=bpy.context.object;o.name=name
    if axis=='Y':o.rotation_euler.x=math.pi/2
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    if bevel:
        m=o.modifiers.new('Edge bevel','BEVEL');m.width=min(bevel,depth*.4,radius*.4)*U;m.segments=2
        bpy.ops.object.modifier_apply(modifier=m.name)
    return surface(o,slot)

def prism_xz(name,outline,front,back,slot,material=None):
    n=len(outline);vv=[(x,front,z) for x,z in outline]+[(x,back,z) for x,z in outline]
    ff=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    ff += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,vv,ff,slot,material=material)

def loop_xz(name,poly,outer,inner,front,back,slot):
    n=len(poly);vv=[]
    for scale,y in ((outer,front),(inner,front),(outer,back),(inner,back)):
        vv += [(x*scale,y,z*scale) for x,z in poly]
    ff=[]
    for i in range(n):
        j=(i+1)%n
        ff.extend([(i,j,n+j,n+i),(2*n+i,3*n+i,3*n+j,2*n+j),
                   (i,2*n+i,2*n+j,j),(n+i,n+j,3*n+j,3*n+i)])
    return mesh(name,vv,ff,slot)

def plus(name,x,y,z,width,height,bar,depth,slot):
    a,b,c=width/2,height/2,bar/2
    pp=[(-c,b),(c,b),(c,c),(a,c),(a,-c),(c,-c),(c,-b),(-c,-b),(-c,-c),(-a,-c),(-a,c),(-c,c)]
    return prism_xz(name,[(x+xx,z+zz) for xx,zz in pp],y-depth/2,y+depth/2,slot)

MODELS=[]
def finish(name,objects,pivot,description):
    select(objects);bpy.ops.object.join();o=bpy.context.object;o.name='SM_'+name
    scene.cursor.location=Vector(pivot)*U;bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    mod=o.modifiers.new('Export triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    o['units']='centimeters';o['front_axis']='-Y';o['asset_kind']='static mesh';o['description']=description
    o['pivot']=str(pivot)
    # Export before moving the meshes into the presentation layout.
    select([o])
    bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,
        object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',use_tspace=True,
        bake_anim=False,path_mode='COPY',embed_textures=True,use_custom_props=True)
    stats={'file':name+'.fbx','mesh':o.name,'vertices':len(o.data.vertices),'triangles':len(o.data.polygons),
           'dimensions_cm':[round(v,3) for v in o.dimensions],'pivot_cm':list(o.location),
           'material_slots':[m.name for m in o.data.materials],'description':description}
    MODELS.append((o,stats));return o

# ---------------------------------------------------------------
# ENERGY SHIELD: solid rim, open hex lattice, optional energy field.
# Front is -Y; rear handle reaches +Y. Pivot at center/hand attachment.
# ---------------------------------------------------------------
before=set(bpy.data.objects)
outline=[(-.38,.70),(.38,.70),(.57,.49),(.51,-.30),(0,-.80),(-.51,-.30),(-.57,.49)]
loop_xz('Shield structural rim',outline,1,.84,-.040,.043,0)
loop_xz('Shield outer chamfer',outline,1.015,.975,-.042,-.005,1)
loop_xz('Cyan perimeter energy rail',outline,.94,.916,-.059,-.044,5)
loop_xz('Inner rim bevel',outline,.854,.827,-.045,.012,2)
panel=[(x*.827,z*.827) for x,z in outline]
prism_xz('Optional translucent field',panel,-.019,-.015,4,field)

# Regular honeycomb, inset to the rim; shared edges deduplicated.
def inside(x,z,poly):
    hit=False
    for (a,b),(c,d) in zip(poly,poly[1:]+poly[:1]):
        if ((b>z)!=(d>z)) and (x<(c-a)*(z-b)/(d-b)+a):hit=not hit
    return hit
edges=set();rad=.064
for row in range(-6,7):
    for col in range(-5,6):
        x=math.sqrt(3)*rad*(col+.5*(row%2));z=row*rad*1.5
        points=[(x+rad*math.cos(math.radians(30)+j*math.tau/6),z+rad*math.sin(math.radians(30)+j*math.tau/6)) for j in range(6)]
        if all(inside(a,b,[(xx*.96,zz*.96) for xx,zz in panel]) for a,b in points):
            for a,b in zip(points,points[1:]+points[:1]):
                edge=tuple(sorted([(round(a[0],5),round(a[1],5)),(round(b[0],5),round(b[1],5))]))
                edges.add(edge)
for i,(a,b) in enumerate(sorted(edges)):
    tube(f'Field hex {i}',[(a[0],-.043,a[1]),(b[0],-.043,b[1])],.00155,4,5,False)

# Broad armor caps on three vertices and subtle perimeter screws.
for i,(x,z) in enumerate([(-.49,.48),(.49,.48),(0,-.66)]):
    poly=[(x-.080,z+.06),(x+.080,z+.06),(x+.065,z-.04),(x,z-.09),(x-.065,z-.04)]
    prism_xz(f'Rim armor cap {i}',poly,-.079,-.030,1)
    cylinder(f'Rim cap insert {i}',(x,-.09,z),.032,.018,0,6,'Y',.002)
    cylinder(f'Rim cap core {i}',(x,-.102,z),.017,.008,6,6,'Y',.001)
for x,z in [(-.28,.64),(.28,.64),(-.465,-.22),(.465,-.22),(-.24,-.46),(.24,-.46)]:
    cylinder('Inset fastener',(x,-.053,z),.013,.008,12,8,'Y',.001)

# Compact hexagonal projector; central chevron remains legible in silhouette.
cylinder('Projector dark base',(0,-.042,.04),.119,.055,0,6,'Y',.004)
cylinder('Projector cyan gasket',(0,-.075,.04),.100,.012,5,6,'Y',.002)
cylinder('Projector face',(0,-.087,.04),.081,.020,1,6,'Y',.002)
prism_xz('Shield chevron',[(-.046,.066),(0,.039),(.046,.066),(.046,.034),(0,.004),(-.046,.034)],-.103,-.095,6)
for sign in (-1,1):
    tube('Projector upper circuit',[(sign*.11,-.052,.085),(sign*.19,-.052,.19),(sign*.19,-.052,.38)],.004,5,6)
    tube('Projector lower circuit',[(sign*.11,-.052,.00),(sign*.21,-.052,-.12),(sign*.21,-.052,-.23)],.003,4,6)

# Back-side handle and mounting plates (real closed mesh).
for z in (-.18,.18):
    box('Rear handle mount',(0,.055,z),(.15,.045,.10),0,.014)
    tube('Rear grip support',[(0,.067,z),(0,.17,z)],.026,1,8)
tube('Rear grip',[(0,.17,-.18),(0,.18,-.13),(0,.18,.13),(0,.17,.18)],.032,7,12)
for z in (-.10,-.06,-.02,.02,.06,.10):
    cylinder('Grip tread',(0,.18,z),.034,.010,12,12,'Z',.001)
shield=finish('EnergyShield',[o for o in bpy.data.objects if o not in before],(0,0,0),
    'Cyan hex energy shield. Center pivot, +Y rear handle. Optional translucent field in its own material slot.')

# ---------------------------------------------------------------
# HEALTH PICKUP: armored red capsule, prominent warm-white plus.
# Bottom-center pivot. About 35 x 30 x 47 cm; no gameplay logic in FBX.
# ---------------------------------------------------------------
before=set(bpy.data.objects)
# Main octagonal canister and dark recessed front panel.
body=box('Health main shell',(0,0,.216),(.308,.215,.328),8,.035)
box('Health front recessed panel',(0,-.117,.225),(.252,.025,.265),0,.023)
box('Health front inset',(0,-.134,.226),(.218,.020,.230),9,.026)
box('Health rear inset',(0,.114,.225),(.225,.022,.24),12,.022)
# Luminous side fluid reservoirs protected by armor ribs.
for s in (-1,1):
    cylinder('Health side reservoir',(s*.133,0,.218),.035,.245,10,16,'Z',.006)
    for z in (.106,.33):
        cylinder('Reservoir cap',(s*.133,0,z),.043,.033,1,12,'Z',.004)
    box('Health side guard',(s*.153,.023,.220),(.025,.105,.168),1,.010)
    box('Side status light',(s*.167,-.025,.245),(.009,.035,.075),10,.002)

# Off-white cap armor with a red seam and bevelled corners.
for z in (.052,.380):
    box('Offwhite armor cap',(0,0,z),(.332,.245,.065),3,.019)
    box('Cap inset',(0,-.126,z),(.20,.013,.025),1,.006)
    for x in (-.126,.126):
        cylinder('Cap fastener',(x,-.118,z),.008,.012,0,8,'Y',.001)
box('Bottom foot',(0,0,.012),(.23,.18,.024),0,.009)
box('Top injector deck',(0,0,.421),(.19,.14,.032),0,.008)
box('Top safety plug',(0,0,.443),(.10,.092,.024),10,.007)

# A raised single-mesh plus icon, no text textures required to recognize it.
plus('Plus dark recess',0,-.150,.242,.161,.161,.054,.012,7)
plus('Health plus glowing face',0,-.159,.242,.139,.139,.043,.008,14)
plus('Rear plus',0,.129,.25,.10,.10,.032,.006,3)
# 3 charge bars, tiny warning chevrons, and a carrying loop.
for x in (-.054,0,.054):
    box('Health charge bar',(x,-.151,.143),(.035,.007,.012),11,.002)
for s in (-1,1):
    prism_xz('Health corner stripe',[(s*.090,.32),(s*.10,.32),(s*.11,.30),(s*.10,.30)],-.150,-.143,3)
tube('Health carrying loop',[(-.060,.075,.405),(-.060,.078,.44),(-.04,.078,.462),(.04,.078,.462),(.060,.078,.44),(.060,.075,.405)],.011,1,10)
health=finish('HealthPickup',[o for o in bpy.data.objects if o not in before],(0,0,0),
    'Red armored health capsule with warm-white plus and orange reservoirs. Bottom-center pivot; static pickup mesh.')

(OUT/'model_manifest.json').write_text(json.dumps({'blender':bpy.app.version_string,'units':'centimeters','front_axis':'-Y',
    'models':[s for o,s in MODELS],'preview_note':'Health pickup is displayed at 3x scale in the lineup; FBX is actual size.'},indent=2))

# ---------------------------------------------------------------
# Studio presentation. All display transforms applied AFTER export.
# ---------------------------------------------------------------
asset_collection=bpy.data.collections.new('01_ASSETS_REAL_SCALE');scene.collection.children.link(asset_collection)
display_models=[]
for original in (shield,health):
    for collection in list(original.users_collection):collection.objects.unlink(original)
    asset_collection.objects.link(original)
    display=original.copy();display.name='STUDIO_PREVIEW_'+original.name
    scene.collection.objects.link(display);display.hide_render=False;display.hide_set(False)
    display['preview_only']=True
    original.hide_render=True;original.hide_set(True)
    display_models.append(display)
shield,health=display_models
shield.location=Vector((-.93,0,1.03))*U;shield.rotation_euler.z=math.radians(-9)
health.location=Vector((.93,0,.22))*U;health.rotation_euler.z=math.radians(7);health.scale=(3,3,3)

def plain(name,color,rough=.5,metallic=0,glow=0):
    m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metallic
    if glow:p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=glow
    return m
ground=plain('Studio navy',(.015,.023,.038),.65)
pedestal=plain('Studio pedestal',(.027,.043,.061),.4,.4)
textmat=plain('Studio labels',(.45,.60,.67),.8,0,.3)
accent=plain('Studio cyan accent',(.02,.34,.44),.5,0,1)
bpy.ops.mesh.primitive_plane_add(size=20000,location=(0,0,-5));floor=bpy.context.object;floor.name='STUDIO_Floor';floor.data.materials.append(ground)
for x,r in ((-.93,.65),(.93,.60)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=64,radius=r*U,depth=10,location=(x*U,0,0))
    o=bpy.context.object;o.name='STUDIO_Plinth';o.data.materials.append(pedestal)
    mod=o.modifiers.new('Plinth bevel','BEVEL');mod.width=2;mod.segments=3
world=bpy.data.worlds.new('StudioWorld');scene.world=world;world.use_nodes=True
world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.04,.07,1)
world.node_tree.nodes['Background'].inputs[1].default_value=.35

def light(name,loc,energy,color,size):
    d=bpy.data.lights.new(name,'AREA');d.energy=energy;d.color=color;d.shape='DISK';d.size=size*U
    o=bpy.data.objects.new(name,d);scene.collection.objects.link(o);o.location=Vector(loc)*U
    o.rotation_euler=(Vector((0,0,.8))*U-o.location).to_track_quat('-Z','Y').to_euler()
light('Key',(-3,-5,6),1200000,(.83,.93,1),5)
light('Warm fill',(4,-3,3),700000,(1,.78,.58),4)
light('Blue rim',(0,3,5),1700000,(.15,.48,1),4)
light('Front',(0,-5,2),280000,(.75,.88,1),3)
d=bpy.data.cameras.new('PresentationCamera');cam=bpy.data.objects.new('PresentationCamera',d);scene.collection.objects.link(cam)
cam.location=Vector((2.5,-10,4))*U;target=Vector((0,0,.87))*U
cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();d.type='ORTHO';d.ortho_scale=4.15*U;d.clip_end=100000;scene.camera=cam

def label(body,loc,size,mat):
    curve=bpy.data.curves.new('STUDIO_Label','FONT');curve.body=body;curve.size=size*U;curve.align_x='CENTER';curve.extrude=0
    o=bpy.data.objects.new('STUDIO_'+body,curve);scene.collection.objects.link(o);o.location=Vector(loc)*U
    o.rotation_euler=cam.rotation_euler;curve.materials.append(mat)
label('ENERGY SHIELD',(-.95,-.32,1.99),.075,textmat)
label('HEALTH PICKUP',(.94,-.32,1.99),.075,textmat)
label('152 cm  /  CENTER PIVOT',(-.93,-.80,.07),.039,textmat)
label('47 cm  /  SHOWN AT 3x',(.94,-.80,.07),.039,textmat)

scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.render.resolution_x=1600;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
scene.use_nodes=True;tree=scene.node_tree;tree.nodes.clear()
rl=tree.nodes.new('CompositorNodeRLayers');gl=tree.nodes.new('CompositorNodeGlare');gl.glare_type='FOG_GLOW';gl.quality='HIGH';gl.threshold=1.6
comp=tree.nodes.new('CompositorNodeComposite');tree.links.new(rl.outputs['Image'],gl.inputs['Image']);tree.links.new(gl.outputs['Image'],comp.inputs['Image'])
for im in bpy.data.images:
    if im.source=='FILE':im.pack();im.filepath='//textures/'+Path(im.filepath).name
for scr in bpy.data.screens:
    for area in scr.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_distance=400;area.spaces.active.region_3d.view_location=(0,0,100)
select([shield,health])
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Shield_Health.blend'))
scene.render.filepath=str(OUT/'preview'/'Shield_Health_Preview.png')
bpy.ops.render.render(write_still=True)
print('PROPS_COMPLETE',json.dumps([s for o,s in MODELS]),flush=True)
