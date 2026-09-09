"""Dedicated headless CPU Blender comparison of actual portable terrain output.

blender --background --threads 4 --python this_file -- <export-directory>
This neutral material isolates shape/normals. It is NOT UE or packaged evidence.
"""
import bpy
import hashlib
import json
import math
import os
import sys
from pathlib import Path
from mathutils import Vector

out = Path(sys.argv[sys.argv.index('--') + 1]).resolve()
data4 = json.loads((out / 'v04.json').read_text(encoding='utf-8'))
data3 = json.loads((out / 'v03.json').read_text(encoding='utf-8'))
assert data3['clastCount'] == data4['clastCount']
hero = max((c for c in data4['ranges'] if 18 < c['center'][0] < 48 and 18 < c['center'][1] < 48), key=lambda c: c['radius'])
center = Vector(hero['center'])
print('PROCESS', os.getpid(), 'CPU_THREADS', 4, 'HERO', list(center), flush=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 24
scene.cycles.use_denoising = True
scene.render.threads_mode = 'FIXED'
scene.render.threads = 4
scene.render.resolution_x = 768
scene.render.resolution_y = 576
scene.render.resolution_percentage = 100
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = 0
scene.world.use_nodes = True
scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value = (.16, .16, .16, 1)
scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value = .08
material = bpy.data.materials.new('NeutralDryRock_geometry_comparison_only')
material.use_nodes = True
material.node_tree.nodes.clear()
bsdf = material.node_tree.nodes.new('ShaderNodeBsdfPrincipled')
output = material.node_tree.nodes.new('ShaderNodeOutputMaterial')
material.node_tree.links.new(bsdf.outputs['BSDF'], output.inputs['Surface'])
bsdf.inputs['Base Color'].default_value = (.18, .18, .18, 1)
bsdf.inputs['Roughness'].default_value = .95
light = bpy.data.lights.new('ControlledSun', 'SUN')
light.energy = 3.0
light.angle = math.radians(.53)
sun = bpy.data.objects.new('ControlledSun', light)
scene.collection.objects.link(sun)
sun.rotation_euler = Vector((.7, -.8, -0.4)).to_track_quat('-Z', 'Y').to_euler()
camdata = bpy.data.cameras.new('LockedComparisonCamera')
camera = bpy.data.objects.new('LockedComparisonCamera', camdata)
scene.collection.objects.link(camera)
scene.camera = camera
camdata.lens = 42
camdata.clip_start = .015
camdata.clip_end = 2000
views = [('near', (.85, -1.0, .70)), ('mid', (3.0, -4.0, 1.7)), ('far', (12.0, -16.0, 7.0))]
receipts = []
for label, data in [('v03', data3), ('v04', data4)]:
    objects = []
    for name in ('ground', 'clasts'):
        d = data[name]
        triangles = [d['indices'][i:i+3] for i in range(0, len(d['indices']), 3)]
        mesh = bpy.data.meshes.new(f'{label}_{name}')
        mesh.from_pydata(d['positions'], [], triangles)
        mesh.update()
        for p in mesh.polygons:
            p.use_smooth = True
        mesh.normals_split_custom_set_from_vertices(d['normals'])
        obj = bpy.data.objects.new(mesh.name, mesh)
        scene.collection.objects.link(obj)
        obj.data.materials.append(material)
        objects.append(obj)
    for view, offset in views:
        camera.location = center + Vector(offset)
        target = center + Vector((0, 0, hero['radius'] * .35))
        camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
        path = out / f'{label}-{view}.png'
        scene.render.filepath = str(path)
        bpy.ops.render.render(write_still=True)
        receipts.append(dict(version=label, view=view, file=path.name,
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                             camera=list(camera.location), target=list(target)))
    for obj in objects:
        bpy.data.objects.remove(obj, do_unlink=True)
receipt = dict(status='CPU_BLENDER_GEOMETRY_COMPARISON_ONLY_NOT_UE', pid=os.getpid(), threads=4,
               renderer='Cycles CPU 24 samples', viewport=[768, 576],
               material='Identical neutral .18 linear gray, roughness .95; no textures',
               hero=hero, clastCount=data4['clastCount'], views=receipts,
               limitations=['No packaged game, GPU materials, texture residency, 4K temporal or frame-time proof',
                            'Directional Apollo comparison, not registered reproduction of a photographed stone'])
(out / 'preview-receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
