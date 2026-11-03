"""Offline delivery check for the dedicated CharacterPipelineSmoke fixture."""
import unreal

base = '/Game/CharacterPipelineSmokeV3'
mesh = unreal.load_asset(base + '/Mesh/Character')
assert isinstance(mesh, unreal.SkeletalMesh), 'Missing skeletal mesh'
skeleton = mesh.get_editor_property('skeleton')
assert isinstance(skeleton, unreal.Skeleton)
clips = []
for name in ('Bend','Step'):
    assets = [unreal.load_asset(p) for p in unreal.EditorAssetLibrary.list_assets(base+'/Animations/'+name)]
    animations = [a for a in assets if isinstance(a, unreal.AnimSequence)]
    assert len(animations) == 1, (name, 'Expected one clip')
    clip = animations[0]
    assert clip.get_editor_property('skeleton') == skeleton
    assert abs(clip.get_play_length() - 1.0) < .04, 'Unexpected clip duration'
    assert clip.get_editor_property('enable_root_motion') == (name == 'Step')
    options = unreal.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = mesh
    options.incorporate_root_motion_into_pose = True
    poses = [unreal.AnimPoseExtensions.get_anim_pose_at_time(clip,t,options) for t in (0,.5,1)]
    assert set(str(n) for n in unreal.AnimPoseExtensions.get_bone_names(poses[0])) == {'root','lower','upper'}
    roots = [unreal.AnimPoseExtensions.get_bone_pose(p,'root') for p in poses]
    assert all(abs(v-1) < .01 for v in (roots[0].scale3d.x, roots[0].scale3d.y, roots[0].scale3d.z)), 'Root scale must be unit scale'
    if name == 'Step':
        assert abs((roots[-1].translation-roots[0].translation).length()-50) < .1, 'Expected 50 cm root motion'
    else:
        rotations = [unreal.AnimPoseExtensions.get_bone_pose(p,'upper').rotation for p in poses]
        dot = abs(sum(getattr(rotations[0],c)*getattr(rotations[1],c) for c in ('x','y','z','w')))
        assert dot < .99, 'Bend animation lost its rotating bone'
    clips.append({'name':name, 'path':clip.get_path_name(), 'duration':clip.get_play_length()})
bounds = mesh.get_bounds()
assert 90 < bounds.box_extent.z < 110, 'Expected a two metre character in centimetres'
result = {'mesh':mesh.get_path_name(), 'skeleton':skeleton.get_path_name(), 'height_cm':bounds.box_extent.z*2, 'clips':clips}
retargeted = unreal.load_asset(base+'/Retargeted/Bend')
if retargeted:
    target = unreal.load_asset(base+'/Target/Character')
    assert target.get_editor_property('skeleton') != skeleton
    assert retargeted.get_editor_property('skeleton') == target.get_editor_property('skeleton')
    options = unreal.AnimPoseEvaluationOptions(); options.optional_skeletal_mesh = target
    poses = [unreal.AnimPoseExtensions.get_anim_pose_at_time(retargeted,t,options) for t in (0,.5)]
    q = [unreal.AnimPoseExtensions.get_bone_pose(p,'upper').rotation for p in poses]
    assert abs(sum(getattr(q[0],c)*getattr(q[1],c) for c in ('x','y','z','w'))) < .99, 'Retargeted bend lost motion'
    result['retargeted'] = retargeted.get_path_name()
