"""Shared geometry and padded texture-atlas helpers for the volcanic planet kit.
Blender 4.5; authored in meters, stored in centimeters.
"""
import bpy, bmesh, math, json, os, random
from pathlib import Path
from mathutils import Vector
import numpy as np
from PIL import Image

OUT=Path(os.environ.get('PLANET_OUTPUT',Path(__file__).resolve().parents[1]))
for p in ['textures','preview']: (OUT/p).mkdir(parents=True,exist_ok=True)
U=100.0
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene
scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01

# One shared padded atlas. Field opacity is a separate named material slot.
COLORS=[(25,30,42),(55,68,84),(105,114,129),(195,197,187),
        (13,112,149),(33,205,245),(148,244,255),(6,15,25),
        (128,27,24),(224,60,30),(255,113,47),(255,217,159),
        (36,25,55),(159,65,235),(255,248,220),(18,117,116)]
EMIT={4:.55,5:1,6:1,9:.3,10:1,11:1,13:.9,14:.7,15:.55}
W=1024;T=256
base=np.zeros((W,W,3),np.uint8);emiss=np.zeros_like(base)
rough=np.zeros((W,W),np.uint8);metal=np.zeros((W,W),np.uint8)
rng=np.random.default_rng(13133);yy,xx=np.mgrid[0:T,0:T]/T
for i,col in enumerate(COLORS):
    r,c=divmod(i,4);s=np.s_[r*T:(r+1)*T,c*T:(c+1)*T]
    grain=rng.normal(0,.035,(T,T));shade=.94+grain+.035*np.sin(xx*24+np.sin(yy*8)*3)
    if i in (0,1,2,3,12,13): shade-=.025*(np.sin(yy*133+xx*3)>.97)
    base[s]=np.clip(np.array(col)[None,None,:]*shade[:,:,None],0,255).astype(np.uint8)
    if i in EMIT: emiss[s]=np.clip(np.array(col)*EMIT[i],0,255).astype(np.uint8)
    rough[s]=int((.27 if i in EMIT else .40)*255)
    metal[s]=int((.65 if i in (0,1,2,12,13) else .08)*255)
for suffix,data in [('BaseColor',base),('Emissive',emiss),('Roughness',rough),('Metallic',metal)]:
    Image.fromarray(data).save(OUT/'textures'/('Planet_'+suffix+'.png'))

atlas=bpy.data.materials.new('M_Planet_Atlas');atlas.use_nodes=True
bs=atlas.node_tree.nodes.get('Principled BSDF');bs.inputs['Emission Strength'].default_value=2.2
for index,(s,inp) in enumerate([('BaseColor','Base Color'),('Emissive','Emission Color'),('Roughness','Roughness'),('Metallic','Metallic')]):
    node=atlas.node_tree.nodes.new('ShaderNodeTexImage')
    node.image=bpy.data.images.load(str(OUT/'textures'/('Planet_'+s+'.png')))
    node.location=(-600,300-index*230);node.label=s
    if s in ('Roughness','Metallic'): node.image.colorspace_settings.name='Non-Color'
    atlas.node_tree.links.new(node.outputs['Color'],bs.inputs[inp])
atlas.diffuse_color=(.08,.30,.40,1)
field=bpy.data.materials.new('M_EnergyField_Transparent');field.use_nodes=True
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

