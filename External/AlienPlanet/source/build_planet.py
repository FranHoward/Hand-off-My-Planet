"""Original modular volcanic alien planet kit. Blender 4.5 / bpy 4.5.14.
Run with: blender --background --python build_planet.py
Requires numpy + Pillow. Assets are centimeters, +Z up, -Y forward.
All FBX objects are static mesh prototypes; no engine gameplay or Niagara systems.
"""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from planet_lib import *

R=random.Random(261017)
ASSETS={};MANIFEST=[];PLACEMENTS=[];ZONES={}
library=bpy.data.collections.new('00_MODULAR_ASSETS_REAL_SCALE');scene.collection.children.link(library)
scene.render.fps=30

def begin():return set(bpy.data.objects)

def export_fbx(path,objects):
    select(objects)
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},
        global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',use_tspace=True,
        bake_anim=False,path_mode='COPY',embed_textures=True,use_custom_props=True)

def finish(name,start,zone,description,collision='simple convex'):
    obs=[o for o in bpy.data.objects if o not in start and o.type=='MESH']
    select(obs);bpy.ops.object.join();ob=bpy.context.object;ob.name='SM_'+name
    scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # Weld numerical duplicates before triangulation, without changing UV loops.
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001)
    bmesh.ops.dissolve_degenerate(bm,dist=.000001,edges=list(bm.edges))
    bmesh.ops.triangulate(bm,faces=list(bm.faces));bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(ob.data);bm.free();ob.data.update()
    ob['asset_id']=name;ob['units']='centimeters';ob['role']=description
    export_fbx(OUT/'fbx/assets'/(name+'.fbx'),[ob])
    stat={'id':name,'file':'fbx/assets/'+name+'.fbx','type':'StaticMesh','zone':zone,
          'vertices':len(ob.data.vertices),'triangles':len(ob.data.polygons),
          'dimensions_cm':[round(x,3) for x in ob.dimensions],
          'materials':[m.name for m in ob.data.materials],'collision_hint':collision,
          'description':description}
    MANIFEST.append(stat);ASSETS[name]=ob
    for c in list(ob.users_collection):c.objects.unlink(ob)
    library.objects.link(ob);ob.hide_render=True;ob.hide_set(True)
    print('ASSET',name,stat['triangles'],flush=True)
    return ob

def rock(name,center,size,slot=0,sub=1,seed=None):
    rr=random.Random(seed) if seed is not None else R
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=Vector(center)*U)
    ob=bpy.context.object;ob.name=name
    for v in ob.data.vertices:
        f=rr.uniform(.83,1.16)
        v.co=Vector((v.co.x*size[0],v.co.y*size[1],v.co.z*size[2]))*U*f
    return surface(ob,slot)

def crystal(name,base,radius,height,slot=5,tilt=(0,0,1),sides=5):
    q=Vector((0,0,1)).rotation_difference(Vector(tilt).normalized())
    vv=[]
    for z,r in [(0,.8),(height*.72,1)]:
        for j in range(sides):
            a=math.tau*j/sides;vv.append((radius*r*math.cos(a),radius*r*math.sin(a),z))
    vv.append((radius*.10,0,height));ff=[tuple(reversed(range(sides)))]
    for j in range(sides):
        k=(j+1)%sides;ff.extend([(j,k,sides+k,sides+j),(sides+j,sides+k,2*sides)])
    return mesh(name,[Vector(base)+q@Vector(v) for v in vv],ff,slot)

def ring(name,center,radius,minor,slot,axis='Z',segments=48,sides=6,start=0,end=math.tau):
    closed=abs(end-start-math.tau)<1e-7;count=segments if closed else segments+1
    vv=[];center=Vector(center)
    for i in range(count):
        a=start+(end-start)*i/segments
        for j in range(sides):
            b=math.tau*j/sides;r=radius+minor*math.cos(b)
            p=(r*math.cos(a),r*math.sin(a),minor*math.sin(b))
            if axis=='Y':p=(p[0],p[2],p[1])
            elif axis=='X':p=(p[2],p[0],p[1])
            vv.append(center+Vector(p))
    ff=[]
    for i in range(segments):
        ni=(i+1)%count
        for j in range(sides):
            k=(j+1)%sides;ff.append((i*sides+j,ni*sides+j,ni*sides+k,i*sides+k))
    if not closed:ff.extend([tuple(reversed(range(sides))),tuple((count-1)*sides+j for j in range(sides))])
    return mesh(name,vv,ff,slot,True)

def prism_xy(name,poly,z0,z1,slot):
    n=len(poly);vv=[(x,y,z0) for x,y in poly]+[(x,y,z1) for x,y in poly]
    ff=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return mesh(name,vv,ff,slot)

def arch_ring(name,radius,width,z,height,slot,start=0,end=math.tau,segments=64):
    vv=[]
    for zz in (z,z+height):
        for rr in (radius-width/2,radius+width/2):
            for j in range(segments+1):
                t=start+(end-start)*j/segments;vv.append((rr*math.cos(t),rr*math.sin(t),zz))
    n=segments+1;ff=[]
    for j in range(segments):
        k=j+1;ff.extend([(j,k,n+k,n+j),(2*n+j,3*n+j,3*n+k,2*n+k),
                        (j,2*n+j,2*n+k,k),(n+j,n+k,3*n+k,3*n+j)])
    ff.extend([(0,n,3*n,2*n),(segments,2*n+segments,3*n+segments,n+segments)])
    return mesh(name,vv,ff,slot)

def rune(name,center,height=1,slot=5):
    x,y,z=center
    tube(name,[(x-.18*height,y,z+.4*height),(x,y,z+.65*height),(x+.18*height,y,z+.4*height),(x,y,z+.15*height),(x-.18*height,y,z+.4*height)],.025*height,slot,6)
    tube(name+' stem',[(x,y,z),(x,y,z+.8*height)],.019*height,slot,6)

# --- Spawn area: damaged explorer lander ---
s=begin()
poly=[(-.35,-4.8),(.35,-4.8),(1.25,-2.7),(1.6,2.3),(.9,3.5),(-1.0,3.35),(-1.65,2.0),(-1.28,-2.7)]
prism_xy('Armored hull',poly,.45,1.35,1)
prism_xy('Pale top armor',[(x*.88,y*.88) for x,y in poly],1.36,1.58,3)
# Angular cockpit has real sloped faces.
mesh('Cockpit shell',[(-.88,-3.0,1.55),(.88,-3,1.55),(.95,-.5,1.58),(-.95,-.5,1.58),
                     (-.52,-2.7,2.20),(.52,-2.7,2.20),(.63,-.8,2.35),(-.63,-.8,2.35)],
     [(0,1,2,3),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0),(4,7,6,5)],7)
tube('Cockpit cyan glass rim',[(-.53,-2.72,2.23),(.53,-2.72,2.23),(.65,-.80,2.38),(-.65,-.80,2.38),(-.53,-2.72,2.23)],.035,5)
for side in (-1,1):
    wing=[(side*.9,-.8),(side*4.4,1.3),(side*4.0,2.5),(side*1.1,2.0)]
    if side==1:wing=[(1.0,-.8),(2.8,.3),(2.25,.8),(2.9,1.4),(2.1,2.2),(1.1,2)]
    prism_xy('Broken swept wing' if side==1 else 'Swept wing',wing,.75,1.06,1)
    tube('Wing guide light',[(side*1.3,.2,1.09),(side*(2.55 if side==1 else 3.85),1.35,1.09)],.028,5)
    cylinder('Engine casing',(side*1.55,2.35,1.32),.50,2.45,0,16,'Y',.06)
    cylinder('Engine exhaust',(side*1.55,3.62,1.32),.39,.09,7,16,'Y',.01)
    ring('Engine orange hot rim',(side*1.55,3.69,1.32),.31,.055,10,'Y',24)
    cylinder('Exhaust core',(side*1.55,3.72,1.32),.15,.03,11,16,'Y',.002)
    for y in (-1.8,1.8):box('Landing foot',(side*1.25,y,.18),(.7,1.1,.35),0,.07)
prism_xy('Detached wing debris',[(2.8,2.4),(5.2,2.7),(4.2,4.1),(2.95,3.5)],.04,.35,3)
for i in range(5):
    x=R.uniform(-1.1,1.1);y=R.uniform(.1,2.3)
    rock('Burnt hull fragments',(x,y,1.70),(.27,.40,.15),0)
tube('Ruptured live cable',[(1.2,1.5,1.62),(1.8,1.0,1.35),(2.0,.5,.68),(2.8,.6,.18)],.055,10)
finish('CrashedShip',s,'01_Spawn','Damaged 9-meter exploration lander with asymmetric broken wing and hot exhaust.')

for name,size,seed in [('BasaltRock_A',(1.35,1.0,1.2),12),('BasaltRock_B',(2.0,1.25,.72),31)]:
    s=begin();rock('Faceted basalt',(0,0,size[2]*.75),size,0,2,seed)
    rock('Rock secondary lobe',(.7,.2,.45),(.8,.6,.6),12,1,seed+1)
    finish(name,s,'Shared','Faceted volcanic rock. Base pivot, variation-ready.')

s=begin();N=12;size=24;vv=[]
for j in range(N+1):
    for i in range(N+1):
        x=-size/2+i*size/N;y=-size/2+j*size/N
        # Flat central route, low banks toward the outer edge, identical seams.
        z=max(0,(abs(x)-6)/6)*(.18+.12*math.sin(y*.8))
        if i in (0,N) or j in (0,N):z=0
        vv.append((x,y,z))
ff=[]
for j in range(N):
    for i in range(N):
        a=j*(N+1)+i;b=a+1;c=a+N+2;d=a+N+1
        ff.extend([(a,b,c),(a,c,d)])
boundary=list(range(N+1))+[j*(N+1)+N for j in range(1,N+1)]+[N*(N+1)+i for i in range(N-1,-1,-1)]+[j*(N+1) for j in range(N-1,0,-1)]
bottom=[]
for index in boundary:bottom.append(len(vv));x,y,z=vv[index];vv.append((x,y,-.8))
for k,a in enumerate(boundary):
    q=(k+1)%len(boundary);ff.append((a,boundary[q],bottom[q],bottom[k]))
ff.append(tuple(reversed(bottom)))
mesh('Terrain tile',vv,ff,0)
for side in (-1,1):
    for k in range(3):
        y=-8+k*7;x=side*R.uniform(8,10)
        tube('Ground basalt fracture',[(x,y,.21),(x-side*.8,y+1.4,.17),(x+side*.4,y+3,.22),(x-side*.2,y+4,.2)],.025,8,5,False)
finish('VolcanicGround_24m',s,'Shared','24 x 24 m ground tile; flat central walking lane. Tile seam elevation zero.','complex static or box')

s=begin()
rock('Plant foot',(0,0,.13),(.40,.33,.22),0)
for k in range(7):
    a=k*2.4;h=R.uniform(.55,1.35);x=math.cos(a)*.32;y=math.sin(a)*.32
    tube('Glowing plant stem',[(0,0,.13),(x*.4,y*.4,h*.5),(x,y,h)],.025,15,6)
    crystal('Luminous leaf',(x,y,h*.75),.095,h*.35,5,(x*.5,y*.5,1),5)
    ring('Plant corona',(x,y,h),.075,.010,6,'Z',12,5)
finish('GlowPlant',s,'01_Spawn','Teal bioluminescent plant with seven crystalline shoots.')

s=begin();cylinder('Beacon foot',(0,0,.15),.7,.3,1,6,'Z',.05)
box('Beacon mast',(0,0,1.0),(.30,.35,1.6),0,.05)
for side in (-1,1):tube('Beacon stripe',[(side*.17,-.2,.35),(side*.17,-.2,1.58)],.025,5)
box('Diegetic display frame',(0,-.06,1.85),(1.08,.20,.66),1,.05)
box('Diegetic display',(0,-.18,1.85),(.91,.028,.50),7,.008)
prism_xz('Navigation arrow',[(-.20,1.85),(0,2.02),(.20,1.85),(.07,1.85),(.07,1.70),(-.07,1.70),(-.07,1.85)],-.205,-.19,5)
for x in (-.37,.37):box('Status indicator',(x,-.205,1.85),(.04,.012,.30),10,.003)
ring('Beacon active marker',(0,0,.32),.56,.025,5,'Z',32)
finish('MissionBeacon',s,'01_Spawn','Diegetic cyan arrow terminal with amber status indicators; static screen geometry.')

# --- Training canyon: modular crystals, wall, barrier, four targets ---
for name,slot in [('CrystalCluster_Cyan',5),('CrystalCluster_Amber',10)]:
    s=begin();rock('Crystal basalt root',(0,0,.18),(1.0,.75,.38),0,1)
    for k,(x,y,h,r) in enumerate([(0,0,2.5,.34),(-.5,.12,1.7,.25),(.55,.15,1.45,.23),(-.20,-.45,.9,.17),(.4,-.4,1.0,.18)]):
        crystal('Energy crystal',(x,y,.20),r,h,slot,(x*.7,y*.5,1),5)
        crystal('Crystal darker facet',(x+r*.25,y+r*.20,.20),r*.45,h*.75,4 if slot==5 else 8,(x*.7,y*.5,1),5)
    finish(name,s,'02_Training','Five faceted energy crystals on volcanic base. Environmental crystals, no freeze gameplay.')

s=begin()
for i in range(9):
    x=-5.8+i*1.45;h=R.uniform(4.7,8.3);r=R.uniform(.85,1.35)
    vv=[]
    for z,rad in [(-.4,r*1.18),(h*.45,r),(h,r*.83)]:
        for j in range(6):
            a=math.tau*j/6+.14;vv.append((x+rad*math.cos(a),rad*math.sin(a)+.1*math.sin(i),z))
    ff=[tuple(reversed(range(6))),tuple(range(12,18))]
    for row in range(2):
        for j in range(6):ff.append((row*6+j,row*6+(j+1)%6,(row+1)*6+(j+1)%6,(row+1)*6+j))
    mesh('Columnar basalt',vv,ff,0 if i%3 else 12)
    rock('Talus stone',(x,-.7,.45),(1.1,.9,.65),0,1)
    if i in (1,4,7):
        tube('Orange mineral fault',[(x-.2,-r*.93,.5),(x+.1,-r*.93,1.8),(x-.12,-r*.93,2.9),(x+.05,-r*.93,4.0)],.042,10,5,False)
finish('CanyonWall_12m',s,'02_Training','12 m columnar basalt wall module with orange mineral faults.','complex static')

s=begin()
for side in (-1,1):
    box('Barrier foot',(side*2,0,.18),(.9,.95,.36),0,.10)
    box('Barrier pillar',(side*2,0,1.8),(.45,.5,3.3),1,.07)
    box('Barrier stripe',(side*1.75,-.02,1.8),(.055,.38,2.9),5,.01)
    crystal('Barrier cap',(side*2,0,3.45),.18,.5,5)
box('Barrier lintel',(0,0,3.42),(4.4,.45,.22),0,.04)
prism_xz('Barrier optional field',[(-1.72,.28),(1.72,.28),(1.72,3.30),(-1.72,3.30)],-.025,.025,4,field)
for i in range(-5,6):
    x=i*.30
    tube('Vertical barrier line',[(x,-.06,.32),(x,-.06,3.25)],.009,4,5)
for j in range(6):tube('Barrier scan line',[(-1.7,-.06,.5+j*.5),(1.7,-.06,.5+j*.5)],.012,5,5)
finish('EnergyBarrier',s,'02_Training','4 m gate with separate transparent field material; opening/blocking logic is external.','Box Collision in Blueprint')

for kind,slot in [('Shield',5),('Fire',10),('Wave',13),('Laser',15)]:
    s=begin();cylinder('Target stand',(0,0,.13),.58,.26,0,8,'Z',.04)
    box('Target stem',(0,.05,.72),(.16,.22,1.25),1,.03)
    cylinder('Target dark plate',(0,0,1.64),.68,.18,1,12,'Y',.025)
    cylinder('Target inset',(0,-.105,1.64),.58,.035,7,24,'Y',.005)
    ring('Target luminous rim',(0,-.15,1.64),.58,.035,slot,'Y',40)
    if kind=='Shield':
        pp=[(-.24,1.92),(.24,1.92),(.24,1.60),(0,1.34),(-.24,1.60)]
        prism_xz('Shield target icon',pp,-.17,-.15,slot)
    elif kind=='Fire':
        pp=[(0,2.04),(.11,1.80),(.21,1.86),(.29,1.61),(.15,1.35),(-.13,1.35),(-.28,1.58),(-.15,1.79),(-.13,1.60)]
        prism_xz('Fire target icon',pp,-.18,-.15,slot)
    elif kind=='Wave':
        for r in (.13,.25,.38):ring('Wave target icon',(0,-.17,1.64),r,.018,slot,'Y',32)
    else:
        for x in (-.32,.32):tube('Laser target reticle',[(x,-.17,1.84),(x,-.17,1.44)],.018,slot,6)
        tube('Laser target ray',[(-.42,-.17,1.64),(.42,-.17,1.64)],.035,slot,6)
        cylinder('Laser target dot',(0,-.19,1.64),.1,.03,6,12,'Y',.002)
    for x in (-.22,0,.22):box('Target status',(x,-.18,1.0),(.12,.02,.035),11,.004)
    finish('TrainingTarget_'+kind,s,'02_Training',kind+' training target with geometric ability icon and three status indicators.')

s=begin()
rock('Drone chassis',(0,0,1.0),(.60,.42,.42),1,2)
cylinder('Drone front socket',(0,-.40,1.0),.29,.16,0,12,'Y',.02)
cylinder('Drone hot sensor',(0,-.50,1.0),.18,.05,10,12,'Y',.005)
ring('Drone sensor rim',(0,-.54,1.0),.23,.022,11,'Y',24)
for side in (-1,1):
    tube('Drone outrigger',[(side*.35,.10,1.0),(side*.82,.15,.9),(side*.88,.1,.65)],.065,1,8)
    prism_xy('Drone armor fin',[(side*.45,-.1),(side*1.0,.2),(side*.75,.6),(side*.40,.3)],.95,1.12,0)
    cylinder('Drone hover pod',(side*.85,.1,.65),.19,.34,1,12,'Z',.025)
    cylinder('Drone hover glow',(side*.85,.1,.46),.13,.035,5,12,'Z',.004)
    tube('Drone fin trim',[(side*.50,-.10,1.15),(side*.92,.2,1.15)],.022,5,6)
tube('Drone antenna',[(0,.1,1.33),(0,.2,1.7)],.025,1,6)
rock('Drone antenna light',(0,.2,1.7),(.055,.055,.055),5,1)
finish('ScoutRobot',s,'02_Training','Hover scout robot; actor movement and attacks can be driven by Blueprint. Static mesh prototype.')

# Additional terrain and set dressing supporting the connected route.
s=begin()
prism_xy('Lava fissure bed',[(-.8,-4),(.5,-4),(.8,-1),(.35,.5),(.9,4),(-.4,4),(-.9,.6),(-.45,-1)],-.12,.02,8)
tube('Lava active center',[(0,-4,.05),(.25,-2.4,.05),(-.2,-.7,.05),(.22,1.2,.05),(0,2.3,.05),(.35,4,.05)],.14,10,6)
for i in range(7):
    y=-3.5+i;side=(-1)**i
    rock('Fissure rubble',(side*.65,y,.16),(.30,.40,.25),0)
finish('LavaFissure_8m',s,'Shared','Orange lava fault strip; place outside the clear player path.','none / damage volume separate')

s=begin();box('Bridge slab',(0,0,.16),(4.8,6,.32),1,.08)
for x in (-2.3,2.3):
    box('Bridge edge',(x,0,.35),(.22,6.1,.18),0,.04)
    tube('Bridge cyan strip',[(x,-2.9,.46),(x,2.9,.46)],.028,5,6)
for y in (-2,-1,0,1,2):box('Bridge transverse tread',(0,y,.345),(4.2,.04,.035),2,.006)
finish('RouteBridge',s,'Shared','4.8 m wide walking bridge with illuminated edge strips.','box')

# --- Star core arena: accessible segmented platform and ancient pylons ---
s=begin();cylinder('Arena foundation',(0,0,-.28),10.2,.9,0,64,'Z',.10)
cylinder('Arena top surface',(0,0,.25),9.9,.2,1,64,'Z',.04)
for i in range(12):
    a=math.tau*i/12+.035;b=math.tau*(i+1)/12-.035
    arch_ring('Arena outer armor',9.55,.65,.36,.13,0,a,b,5)
    arch_ring('Arena cyan circuit',8.9,.075,.36,.025,5,a,b,6)
    arch_ring('Arena inner amber circuit',4.0,.055,.36,.025,10,a,b,5)
    t=(a+b)/2
    tube('Arena radial groove',[(4.2*math.cos(t),4.2*math.sin(t),.365),(8.3*math.cos(t),8.3*math.sin(t),.365)],.025,7,5)
ring('Core charging socket',(0,0,.37),1.8,.065,5,'Z',64)
for r in (2.2,2.45):ring('Inner engraved ring',(0,0,.36),r,.015,2,'Z',64)
# Ramp at -Y joins the ground to the elevated platform top.
mesh('Arena entry ramp',[(-2.2,-13,0),(2.2,-13,0),(2.2,-9,.36),(-2.2,-9,.36),
                         (-2.2,-13,-.5),(2.2,-13,-.5),(2.2,-9,-.5),(-2.2,-9,-.5)],
     [(0,1,2,3),(4,7,6,5),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)],1)
for x in (-2.05,2.05):tube('Entry ramp light',[(x,-12.9,.05),(x,-9,.41)],.027,5,6)
finish('ArenaPlatform',s,'03_Arena','20 m diameter combat floor with entry ramp and concentric energy rings.','complex static; do not use a single convex hull')

s=begin();cylinder('Pillar plinth',(0,0,.25),.95,.5,0,6,'Z',.06)
prism_xy('Ancient pillar shaft',[(-.5,-.38),(.5,-.38),(.65,.1),(.35,.50),(-.4,.5),(-.65,.1)],.45,4.8,0)
prism_xy('Ancient cap',[(-.65,-.48),(.65,-.48),(.78,.15),(.4,.65),(-.4,.65),(-.78,.15)],4.8,5.05,1)
for z in (.8,2.1,3.4):rune('Pillar rune',(0,-.405,z),1.0,5)
for x in (-.42,.42):tube('Pillar vertical energy',[(x,-.395,.65),(x,-.395,4.6)],.025,4,5)
crystal('Pillar cap crystal',(0,0,5.08),.26,.7,5)
finish('AlienPillar',s,'03_Arena','5.8 m basalt obelisk with three readable geometric cyan runes.')

s=begin();cylinder('Core socket',(0,0,.20),1.0,.4,0,12,'Z',.06)
cylinder('Core stepped base',(0,0,.48),.70,.22,1,12,'Z',.03)
ring('Core base coil',(0,0,.63),.62,.055,5,'Z',48)
rock('Floating star nucleus',(0,0,1.75),(.55,.55,.65),5,2)
crystal('Star vertical crystal',(0,0,1.0),.26,1.65,6)
ring('Core orbit vertical',(0,0,1.75),1.03,.055,1,'Y',64)
ring('Core orbit cyan',(0,0,1.75),.98,.022,5,'Y',64)
ring('Core orbit horizontal',(0,0,1.75),.83,.038,10,'Z',48)
for i in range(5):
    a=i*math.tau/5
    crystal('Core floating splinter',(.86*math.cos(a),.86*math.sin(a),1.4+.25*math.sin(a)),.12,.55,5,(math.cos(a)*.5,math.sin(a)*.5,1))
finish('StarCore',s,'03_Arena','Floating cyan star core in an orbital charging cradle. Energy and orbit motion require Blueprint.')

s=begin();cylinder('Pylon base',(0,0,.18),.8,.36,0,8,'Z',.05)
for side in (-1,1):
    tube('Pylon fork',[(side*.45,0,.25),(side*.50,0,1.1),(side*.25,0,1.9)],.13,1,6)
    tube('Pylon fork illumination',[(side*.45,-.12,.45),(side*.45,-.12,1.1),(side*.23,-.12,1.8)],.026,5,6)
crystal('Pylon power crystal',(0,0,.75),.20,1.05,5)
ring('Pylon projected halo',(0,0,2.1),.35,.025,5,'Z',32)
finish('EnergyPylon',s,'03_Arena','Forked charging pylon; use its top center as a beam endpoint.')

# Boss silhouette: hulking volcanic guardian with exposed orange weak point.
s=begin()
for side in (-1,1):
    box('Guardian foot',(side*.88,-.22,.25),(1.25,1.65,.50),0,.14)
    rock('Guardian shin',(side*.85,.05,1.0),(.55,.55,.85),0,1)
    rock('Guardian knee',(side*.83,-.16,1.65),(.57,.55,.48),1,1)
    rock('Guardian thigh',(side*.70,.08,2.15),(.62,.62,.85),0,1)
    tube('Leg molten seam',[(side*.86,-.56,.55),(side*.85,-.57,1.1),(side*.78,-.60,1.7)],.055,10,6)
rock('Guardian pelvis',(0,.05,2.55),(1.1,.70,.6),0,1)
rock('Guardian body',(0,.12,3.65),(1.60,.85,1.25),0,2)
rock('Guardian back mantle',(0,.55,4.0),(1.50,.70,1.15),12,1)
cylinder('Heart socket',(0,-.76,3.64),.62,.27,7,10,'Y',.03)
ring('Heart molten border',(0,-.94,3.64),.48,.07,10,'Y',40)
rock('Exposed star heart',(0,-.96,3.64),(.35,.20,.43),11,2)
for side in (-1,1):
    rock('Shoulder armor',(side*1.58,.07,4.32),(.83,.77,.83),0,1)
    rock('Upper arm',(side*1.92,.08,3.62),(.47,.5,.75),1,1)
    rock('Elbow',(side*2.08,-.03,3.0),(.48,.48,.45),0,1)
    rock('Massive forearm',(side*2.18,-.18,2.43),(.66,.67,.91),0,1)
    rock('Guardian fist',(side*2.14,-.37,1.83),(.70,.66,.52),1,1)
    tube('Arm molten seam',[(side*1.55,-.60,4.48),(side*1.8,-.54,3.7),(side*2.1,-.58,3.0),(side*2.17,-.78,2.3)],.07,10,6)
    for j in range(3):crystal('Shoulder basalt spire',(side*(1.25+j*.32),.1,4.55),.23,.9-j*.12,0,(side*.35,0,1))
    for j in range(3):box('Guardian knuckle',(side*(1.80+j*.22),-.95,1.85),(.19,.18,.26),8,.025)
rock('Guardian neck',(0,.05,4.65),(.50,.45,.45),1,1)
rock('Guardian head',(0,-.03,5.15),(.63,.56,.66),0,1)
prism_xz('Angular face mask',[(-.43,5.40),(.43,5.40),(.37,5.0),(0,4.83),(-.37,5.0)],-.61,-.38,1)
for side in (-1,1):
    prism_xz('Guardian eye',[(side*.08,5.25),(side*.35,5.32),(side*.31,5.14),(side*.08,5.12)],-.634,-.62,10)
    crystal('Guardian crown horn',(side*.44,.07,5.48),.18,.78,0,(side*.45,.1,1))
for z in (3.0,4.45):tube('Chest energy scar',[(-.68,-.71,z),(0,-.85,z-.12),(.68,-.71,z)],.045,10,6)
finish('Boss_StarcoreGuardian',s,'03_Arena','6.2 m volcanic guardian with orange exposed heart and angular horns. Static boss prototype, no rig or animation.')

# Mesh components for Niagara; these are NOT particle systems.
s=begin();rock('Firebolt luminous core',(0,0,0),(.16,.30,.16),11,2)
for k in range(5):
    a=k*math.tau/5;crystal('Firebolt tapered flame',(.12*math.cos(a),.13,.12*math.sin(a)),.08,.60,10,(math.cos(a)*.1,1,math.sin(a)*.1))
finish('FXM_FireBolt',s,'VFX_Meshes','Orange projectile mesh facing -Y. Assign to Niagara Mesh Renderer; trails and sparks are not included.','none')
s=begin();rock('Wave orb',(0,0,0),(.36,.36,.36),13,2)
for axis in ('Y','Z'):ring('Wave orbital band',(0,0,0),.54,.024,13,axis,40)
finish('FXM_WaveOrb',s,'VFX_Meshes','Purple wave/gravity orb mesh. Refraction, distortion and pulse timing are engine effects.','none')
s=begin();ring('Radial wave ring',(0,0,0),1,.045,13,'Z',64)
finish('FXM_WaveRing',s,'VFX_Meshes','Unit-radius purple expanding ring mesh. Animate scale/opacity in Niagara.','none')
s=begin();crystal('Cyan debris shard',(0,0,0),.10,.48,5,(.1,0,1))
finish('FXM_CrystalShard',s,'VFX_Meshes','Cyan crystal debris mesh for environmental bursts or shield hits; not an added combat ability.','none')
s=begin();ring('Teal convergence ring',(0,0,0),.40,.025,15,'Y',40)
for i in range(6):
    a=i*math.tau/6;rock('Convergence point',(.28*math.cos(a),0,.28*math.sin(a)),(.045,.045,.045),6,1)
finish('FXM_TealBeamEndpoint',s,'VFX_Meshes','Teal endpoint mesh for Laser or star-core charging/repair. Niagara beam and particles are separate.','none')

(OUT/'asset_manifest.json').write_text(json.dumps({'units':'centimeters','forward':'-Y','up':'+Z','blender':bpy.app.version_string,'assets':MANIFEST},indent=2))
print('ALL_ASSETS_EXPORTED',len(ASSETS),flush=True)
exec(compile((Path(__file__).with_name('assemble_scene.py')).read_text(),str(Path(__file__).with_name('assemble_scene.py')),'exec'),globals())
