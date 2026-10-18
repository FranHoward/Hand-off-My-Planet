"""Executed by build_planet.py with its mesh library in scope."""

for zone in ('01_Spawn','02_Training','03_Arena'):
    collection=bpy.data.collections.new(zone);scene.collection.children.link(collection)
    ZONES[zone]=collection

def place(asset,zone,loc=(0,0,0),rotation=0,scale=1):
    original=ASSETS[asset];ob=bpy.data.objects.new(asset+'_Placed',original.data)
    ZONES[zone].objects.link(ob);ob.location=Vector(loc)*U
    ob.rotation_euler.z=math.radians(rotation)
    ob.scale=(scale,)*3 if isinstance(scale,(float,int)) else scale
    ob['asset_id']=asset;ob['zone']=zone
    PLACEMENTS.append({'object':ob.name,'asset':asset,'zone':zone,
        'location_cm':[round(v,4) for v in ob.location],
        'rotation_deg':[0,0,rotation],'scale':list(ob.scale)})
    return ob

# One continuous 96-meter route; identical ground seams.
place('VolcanicGround_24m','01_Spawn',(0,0,0))
for y in (24,48):place('VolcanicGround_24m','02_Training',(0,y,0))
place('VolcanicGround_24m','03_Arena',(0,72,0))
place('CrashedShip','01_Spawn',(-3,-.5,0),-24)
place('MissionBeacon','01_Spawn',(2.2,-6.5,0),-14)
place('MissionBeacon','01_Spawn',(0,9,0),0,.8)
for i,(x,y) in enumerate([(-8,-6),(7,-7),(-8,5),(8,6),(-6,9),(7,9)]):
    place('BasaltRock_A' if i%2 else 'BasaltRock_B','01_Spawn',(x,y,0),R.uniform(0,360),R.uniform(.7,1.4))
for x,y in [(-6,-5),(-5,-7),(4,-6),(6,4),(-7,7),(4,8),(-8,-1),(7,-1)]:
    place('GlowPlant','01_Spawn',(x,y,.05),R.uniform(0,360),R.uniform(.7,1.4))
for x,y in [(-9,3),(9,-2)]:place('LavaFissure_8m','01_Spawn',(x,y,0),R.uniform(-15,15),.8)
place('CrystalCluster_Cyan','01_Spawn',(7.2,3,0),15,.75)

# Canyon sides, keeping a 10 m central traversable lane and a 4 m gateway.
for side in (-1,1):
    for j,y in enumerate((12,25,38,51)):
        zone='01_Spawn' if j==0 else '02_Training'
        place('CanyonWall_12m',zone,(side*11.5,y,-.1),90 if side<0 else -90,(1,1,R.uniform(.80,1.13)))
    for y in (20,34,46):
        place('LavaFissure_8m','02_Training',(side*8.4,y,0),R.uniform(-10,10),.8)
place('EnergyBarrier','02_Training',(0,18,0))
place('RouteBridge','02_Training',(0,21,.02))
for k,kind in enumerate(('Shield','Fire','Wave','Laser')):
    y=26+k*5.8;x=-3.8 if k%2==0 else 3.8
    place('TrainingTarget_'+kind,'02_Training',(x,y,0),-12 if x>0 else 12,1.15)
    place('MissionBeacon','02_Training',(-x*.62,y-2.0,0),0,.65)
    place('CrystalCluster_Cyan' if k%2==0 else 'CrystalCluster_Amber','02_Training',(x*1.9,y+1.5,0),R.uniform(0,360),1.1)
for x,y in [(-6,22),(6,26),(-6,33),(6,37),(-6,45)]:
    place('BasaltRock_B','02_Training',(x,y,0),R.uniform(0,360),R.uniform(.7,1.1))
    place('GlowPlant','02_Training',(x+1,y+1,0),R.uniform(0,360),.8)
place('ScoutRobot','02_Training',(1.6,35,.35),-12)
place('ScoutRobot','02_Training',(-1.5,44,.5),18,.85)
place('EnergyBarrier','02_Training',(0,49,0))

# Boss arena. Core occupies the central charge socket; boss initially behind it.
place('ArenaPlatform','03_Arena',(0,67,0))
place('StarCore','03_Arena',(0,67,.4))
place('Boss_StarcoreGuardian','03_Arena',(0,71.7,.4))
for angle in (0,40,80,120,160,200,340):
    a=math.radians(angle)
    place('AlienPillar','03_Arena',(9.1*math.cos(a),67+9.1*math.sin(a),.38),angle-90)
for angle in (35,145,215,325):
    a=math.radians(angle)
    place('EnergyPylon','03_Arena',(6.6*math.cos(a),67+6.6*math.sin(a),.38),angle-90)
for side in (-1,1):
    place('CanyonWall_12m','03_Arena',(side*12,66,-.1),90 if side<0 else -90,(1.25,1,.95))
    place('LavaFissure_8m','03_Arena',(side*10.5,70,0),0,1)
    place('CrystalCluster_Amber','03_Arena',(side*8.2,78,0),20,1.2)
    place('CrystalCluster_Cyan','03_Arena',(side*7.5,56,0),0,.8)
    for y in (57,79):place('BasaltRock_B','03_Arena',(side*9,y,0),R.uniform(0,360),1.4)
place('MissionBeacon','03_Arena',(3.0,54,0))
place('CanyonWall_12m','03_Arena',(0,81,-.1),0,(1.65,1,1.1))

# Export each zone and the full layout with object transforms preserved.
scene_stats=[]
for zone,collection in ZONES.items():
    objects=list(collection.objects)
    path=OUT/'fbx/scenes'/(zone+'_Layout.fbx');export_fbx(path,objects)
    scene_stats.append({'file':str(path.relative_to(OUT)),'objects':len(objects),
        'triangles':sum(len(o.data.polygons) for o in objects)})
all_instances=[o for c in ZONES.values() for o in c.objects]
export_fbx(OUT/'fbx/scenes/AlienPlanet_FullLayout.fbx',all_instances)
scene_stats.append({'file':'fbx/scenes/AlienPlanet_FullLayout.fbx','objects':len(all_instances),
    'triangles':sum(len(o.data.polygons) for o in all_instances)})
(OUT/'scene_layout.json').write_text(json.dumps({'units':'centimeters','coordinates':'Blender right-handed; X right, Y route, Z up',
    'player_start_cm':[0,-850,120],'zone_centers_cm':{'01_Spawn':[0,0,0],'02_Training':[0,3400,0],'03_Arena':[0,6700,0]},
    'instances':PLACEMENTS,'exports':scene_stats},indent=2))

# Rendering scene and atmospheric lighting are NOT part of the FBX exports.
studio=bpy.data.collections.new('99_PREVIEW_LIGHTS_AND_BACKGROUND');scene.collection.children.link(studio)
world=bpy.data.worlds.new('Volcanic sky');scene.world=world;world.use_nodes=True
world.node_tree.nodes['Background'].inputs[0].default_value=(.034,.052,.083,1)
world.node_tree.nodes['Background'].inputs[1].default_value=.40

def area(name,loc,target,energy,color,size):
    d=bpy.data.lights.new(name,'AREA');d.energy=energy;d.color=color;d.shape='DISK';d.size=size*U
    ob=bpy.data.objects.new(name,d);studio.objects.link(ob);ob.location=Vector(loc)*U
    ob.rotation_euler=(Vector(target)*U-ob.location).to_track_quat('-Z','Y').to_euler()
for y in (0,32,67):
    area('Cold skylight',(0,y,24),(0,y,0),15000000,(.45,.67,1),28)
    area('Soft warm key',(-16,y-10,16),(0,y,2),18000000,(1,.84,.70),22)
    area('Orange horizon',(12,y+10,7),(0,y,2),7000000,(1,.29,.07),15)
d=bpy.data.lights.new('Distant starlight','SUN');d.energy=1.1;d.color=(.67,.78,1);d.angle=.30
ob=bpy.data.objects.new('Distant starlight',d);studio.objects.link(ob);ob.rotation_euler=(.45,-.55,-.45)

camdata=bpy.data.cameras.new('PreviewCamera');cam=bpy.data.objects.new('PreviewCamera',camdata);studio.objects.link(cam)
camdata.type='ORTHO';camdata.clip_end=100000;scene.camera=cam
scene.render.engine='CYCLES';scene.cycles.device='CPU';scene.cycles.samples=24;scene.cycles.use_denoising=True
scene.render.resolution_x=1600;scene.render.resolution_y=1050;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
scene.use_nodes=True;tree=scene.node_tree;tree.nodes.clear()
rl=tree.nodes.new('CompositorNodeRLayers');gl=tree.nodes.new('CompositorNodeGlare');gl.glare_type='FOG_GLOW';gl.quality='HIGH';gl.threshold=1.6
comp=tree.nodes.new('CompositorNodeComposite');tree.links.new(rl.outputs['Image'],gl.inputs['Image']);tree.links.new(gl.outputs['Image'],comp.inputs['Image'])

VIEWS=[
    ('01_Spawn_Preview',(-22,-30,22),(0,0,1.4),31,{'01_Spawn'}),
    ('02_Training_Preview',(-19,9,23),(0,34,1.7),42,{'02_Training'}),
    ('03_Arena_Preview',(-22,39,26),(0,68,2.3),34,{'03_Arena'}),
    ('00_FullMap_Preview',(62,-45,91),(0,36,0),103,set(ZONES))]

def set_view(view):
    name,loc,target,scale,visible=view
    cam.location=Vector(loc)*U;cam.rotation_euler=(Vector(target)*U-cam.location).to_track_quat('-Z','Y').to_euler();camdata.ortho_scale=scale*U
    for zone,col in ZONES.items():col.hide_render=zone not in visible
    return name
set_view(VIEWS[3])
for im in bpy.data.images:
    if im.source=='FILE':im.pack();im.filepath='//textures/'+Path(im.filepath).name
for scr in bpy.data.screens:
    for ar in scr.areas:
        if ar.type=='VIEW_3D':
            ar.spaces.active.shading.type='MATERIAL';ar.spaces.active.region_3d.view_distance=11000
            ar.spaces.active.region_3d.view_location=(0,3400,0)
select([o for o in ZONES['03_Arena'].objects if o.get('asset_id')=='Boss_StarcoreGuardian'])
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'AlienPlanet_ThreeZones.blend'))
if os.environ.get('SKIP_RENDER')!='1':
    for view in VIEWS:
        name=set_view(view);scene.render.filepath=str(OUT/'preview'/(name+'.png'))
        bpy.ops.render.render(write_still=True)
        print('PREVIEW_COMPLETE',name,flush=True)
print('PLANET_BUILD_COMPLETE',len(ASSETS),len(PLACEMENTS),flush=True)
