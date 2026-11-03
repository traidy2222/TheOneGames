"""Executed in the explicit Unreal project through the existing native MCP connection."""
import unreal


def load(path, cls):
    asset = unreal.load_asset(path)
    if not isinstance(asset, cls): raise ValueError('Missing or incompatible Unreal asset: ' + path)
    return asset


def run(request):
    library = unreal.EditorAssetLibrary
    destination = request['destination']
    if library.list_assets(destination, recursive=True, include_folder=False):
        raise ValueError('Destination contains assets; use a new folder, not an implicit overwrite')
    if request['operation'] == 'import':
        only_animation = request.get('animation_only', False)
        skeleton = load(request['skeleton'], unreal.Skeleton) if request.get('skeleton') else None
        if only_animation and skeleton is None: raise ValueError('Animation import requires an explicit target Skeleton asset')
        options = unreal.FbxImportUI()
        options.automated_import_should_detect_type = False
        options.import_as_skeletal = True
        options.import_mesh = not only_animation
        options.import_animations = only_animation
        options.import_materials = not only_animation
        options.import_textures = not only_animation
        options.create_physics_asset = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION if only_animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH
        if skeleton: options.skeleton = skeleton
        if only_animation:
            options.anim_sequence_import_data.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        task = unreal.AssetImportTask()
        task.filename = request['source']; task.destination_path = destination
        task.automated = True; task.replace_existing = False; task.save = True; task.options = options
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        assets = [unreal.load_asset(p) for p in task.imported_object_paths]
        wanted = unreal.AnimSequence if only_animation else unreal.SkeletalMesh
        if not any(isinstance(a, wanted) for a in assets): raise ValueError('Importer produced no expected skeletal mesh/animation; inspect Unreal logs')
        rows = []
        for asset in assets:
            if asset is None: raise ValueError('Imported asset failed to load')
            row = {'path': asset.get_path_name(), 'class': asset.get_class().get_name()}
            if isinstance(asset, (unreal.SkeletalMesh, unreal.AnimSequence)):
                target_skeleton = asset.get_editor_property('skeleton')
                if not target_skeleton: raise ValueError('Imported asset has no skeleton')
                row['skeleton'] = target_skeleton.get_path_name()
                if skeleton and target_skeleton != skeleton: raise ValueError('Importer selected a different skeleton')
            if isinstance(asset, unreal.AnimSequence):
                row['duration_seconds'] = asset.get_play_length()
                if row['duration_seconds'] <= 0: raise ValueError('Imported animation has no duration')
                asset.set_editor_property('enable_root_motion', request.get('enable_root_motion', False))
                library.save_loaded_asset(asset)
            rows.append(row)
        return {'assets': rows, 'physics_asset_created': False}
    source_mesh = load(request['source_mesh'], unreal.SkeletalMesh)
    target_mesh = load(request['target_mesh'], unreal.SkeletalMesh)
    retargeter = load(request['retargeter'], unreal.IKRetargeter)
    data = []
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for path in request['animations']:
        animation = load(path, unreal.AnimSequence)
        if animation.get_editor_property('skeleton') != source_mesh.get_editor_property('skeleton'):
            raise ValueError('Source animation and mesh use different skeletons')
        data.append(registry.get_asset_by_object_path(animation.get_path_name()))
    outputs = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets_to_retarget=data, source_mesh=source_mesh, target_mesh=target_mesh,
        ik_retarget_asset=retargeter, target_path=destination, use_source_path=False,
        include_referenced_assets=False, overwrite_existing_files=False)
    rows = []
    for entry in outputs:
        asset = entry.get_asset()
        if not isinstance(asset, unreal.AnimSequence): continue
        if asset.get_editor_property('skeleton') != target_mesh.get_editor_property('skeleton'):
            raise ValueError('Retargeted clip does not use the target skeleton')
        if asset.get_play_length() <= 0: raise ValueError('Retargeted animation has no duration')
        library.save_loaded_asset(asset)
        rows.append({'path': asset.get_path_name(), 'duration_seconds': asset.get_play_length()})
    if len(rows) != len(data): raise ValueError('Retargeter did not produce all requested clips; inspect logs/output folder')
    return {'retargeted': rows, 'retargeter': retargeter.get_path_name(),
            'note': 'Uses the existing configured IK Retargeter; this does not infer chain mappings or prove foot contact quality.'}
