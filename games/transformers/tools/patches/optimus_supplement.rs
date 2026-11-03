// MIT License; local extension to HUNK2/rotf-robots (MIT), commit dd97296c9a618d06cc2e01bf38c6221b057f6eba.
// Reads the user's staged data only. Adds source-backed intros, unbound mesh geometry,
// all original texture payloads, and raw channels that have no proven target object.
use std::{collections::{HashSet,BTreeSet,BTreeMap},path::Path};
use serde_json::{json,Value};
use tf2_core::formats::{anim::{self,Clip},lxb::{DataFile,Node},pack::{self,Pack},scene::{ObjectDef,NodeDef,RenderDef},hash::{crc32,lower33}};
use rotf_export::{model::{self,ClipOut,Shared},names::Names,Piece,PieceKind};
const IDENTITY:[[f32;3];4]=[[1.,0.,0.],[0.,1.,0.],[0.,0.,1.],[0.,0.,0.]];

fn empty(nodes:Vec<NodeDef>,renders:Vec<RenderDef>)->ObjectDef {
    ObjectDef { nodes,renders,emitters:vec![],lights:vec![],spawn_sound_players:vec![],render_sets:vec![],collisions:vec![],spawners:vec![],effects:vec![],skid_marks:vec![],climb_steps:vec![],sound_players:vec![],spine_links:vec![],ribbons:vec![],collision_fx:vec![],collision_sounds:vec![],footsteps:vec![] }
}
fn loose_collection(meshes:&[&tf2_core::formats::model::Mesh],bones:Option<&Vec<tf2_core::formats::model::Bone>>,bone_names:&BTreeMap<u32,String>)->ObjectDef{
    let mut nodes=Vec::new();let mut renders=Vec::new();
    if let Some(bones)=bones{for bone in bones{
        let bind=model::matrix(&bone.inverse_bind).inverse();
        assert!(bind.is_finite(),"Invalid source inverse bind");
        let name=bone_names.get(&bone.name).cloned().unwrap_or_else(||format!("SourceBone_{:08x}",bone.name));
        // Independent native bind frames; this is not an inferred original parent hierarchy.
        nodes.push(NodeDef{name:name.clone(),name_hash:crc32(name.as_bytes()),bone_hash:bone.name,parent:None,bind:std::array::from_fn(|r|{let c=bind.col(r);[c.x,c.y,c.z]})});
    }}
    for mesh in meshes{
        let i=nodes.len();nodes.push(NodeDef{name:mesh.name.clone(),name_hash:crc32(mesh.name.as_bytes()),bone_hash:lower33(&mesh.name),parent:None,bind:IDENTITY});
        renders.push(RenderDef{node:i,model:mesh.name.clone(),lod_distances:vec![]});
    }
    empty(nodes,renders)
}
fn channel_json(c:&Clip,names:&Names)->Vec<Value> {
    c.names.iter().zip(&c.channels).map(|(hash,ch)|json!({"name":names.name(*hash),"hash":format!("{hash:08x}"),
        "position":{"ticks":ch.position.times(),"values":ch.position.values()},
        "rotation":{"ticks":ch.rotation.times(),"values":ch.rotation.values()},
        "scale":{"ticks":ch.scale.times(),"values":ch.scale.values()},
        "own_scale":{"ticks":ch.own_scale.times(),"values":ch.own_scale.values()}
    })).collect()
}
fn raw_properties(node:Node<'_>)->Vec<Value> {
    node.get("streams").into_iter().flat_map(Node::items).filter_map(|s|{
        let ty=s.get("streamType").and_then(Node::int)?;
        if [0,1,2,3,4,5,0x40,0x41,0x42].contains(&ty) {return None}
        let storage:Vec<_>=["frame","keys"].iter().filter_map(|which|{
            let k=s.get(which)?;let class=k.type_name()?;
            let records:Vec<_>=k.get("array").into_iter().flat_map(Node::items).map(|k|json!({"index":k.get("index").and_then(Node::int),"tick":k.get("time").and_then(Node::float),"float":k.get("value").and_then(Node::float),"floats":k.get("value").map(Node::floats)})).collect();
            Some(json!({"storage":which,"class":class,"records":records}))
        }).collect();
        Some(json!({"stream_type":ty,"key_index_map":s.get("keyIndexMap").map(Node::ints),"storage":storage}))
    }).collect()
}
fn extra_native_data(node:Node<'_>,file:&DataFile,seen:&mut HashSet<(usize,String,Vec<u32>,usize)>,properties:&mut Vec<Value>,events:&mut Vec<Value>){
    let fields=node.fields();let ty=node.type_name().unwrap_or("");
    if let Some(at)=node.offset(){if !seen.insert((at,ty.to_owned(),fields.iter().map(|v|v.0).collect(),node.len())){return}}
    if ty=="AnimFile"{let raw=raw_properties(node);if !raw.is_empty(){properties.push(json!({"source_offset":node.offset(),"source_label":node.label().map(|h|file.name(h).map(str::to_owned).unwrap_or_else(||format!("#{h:08x}"))),"streams":raw}));}}
    if ty=="AnimInteractEvent"{
        let mut out=serde_json::Map::new();
        for(h,n)in &fields{out.insert(file.name(*h).map(str::to_owned).unwrap_or_else(||format!("#{h:08x}")),json!({"type":n.type_name(),"integer":n.int(),"float":n.float(),"text":n.text(),"floats":n.floats(),"offset":n.offset(),"label":n.label()}));}
        events.push(json!({"source_offset":node.offset(),"class":ty,"fields":out,"note":"Original interaction event parameters retained; no interaction behavior inferred."}));
    }
    for(_,child)in fields{extra_native_data(child,file,seen,properties,events)}
    for child in node.items(){extra_native_data(child,file,seen,properties,events)}
}
fn clip_out<'a>(name:&str,c:&'a Clip)->ClipOut<'a>{let(set,id)=name.split_once('.').unwrap_or(("",name));ClipOut{name:name.into(),clip:c,looping:false,additive:false,attack:None,extras:json!({"rotf_set":set,"rotf_id":id,"looping":false,"speed":1,"length_ticks":c.length})}}

fn run(install:&Path,exported:&Path,pack_name:&str)->Value {
    let path=install.join("characters").join(format!("{pack_name}.str"));
    let pack=Pack::open(&path).unwrap();
    let chunk=pack.of_type(pack::DATA).next().unwrap();
    let file=DataFile::parse(pack.data(chunk).to_vec()).unwrap();
    let top=file.root();
    let character=top.fields().into_iter().flat_map(|(_,r)|r.get("things").into_iter().flat_map(Node::items)).find(|t|t.type_name()==Some("Character")).unwrap();
    let player_node=character.get("introPlayerAnim").unwrap();
    let camera_node=character.get("introCameraAnim").unwrap();
    let player=anim::read_clip(&file,player_node).unwrap();
    let mut camera=anim::read_clip(&file,camera_node).unwrap();
    assert_eq!(camera.names.len(),1,"Do not infer a multi-channel camera hierarchy");
    let camera_source_hashes=camera.names.clone();
    // The float projection/property stream is kept verbatim in the JSON, not guessed as TRS.
    // The camera's sole named motion channel needs no inferred joint hierarchy.
    for c in &mut camera.channels {c.scale=Default::default();c.own_scale=Default::default();}
    let data=tf2_core::character::load(install,pack_name).unwrap();
    let mut names=Names::load(install,rotf_export::names::data_files(&path),&[]);
    let base_pieces=rotf_export::pieces(&data,pack_name,&names);
    let mut original_files:BTreeMap<u32,String>=BTreeMap::new();
    let mut bone_names=BTreeMap::new();
    for p in &base_pieces{
        for r in &p.object.renders{original_files.entry(lower33(&r.model)).or_insert(format!("{}.glb",p.file));}
        for n in &p.object.nodes{bone_names.entry(n.bone_hash).or_insert(n.name.clone());}
    }
    let mut lods=BTreeSet::new();let mut pending:Vec<_>=original_files.keys().copied().collect();
    while let Some(id)=pending.pop(){if let Some(m)=data.library.meshes.get(&id){for &(id,_)in &m.lods{if !original_files.contains_key(&id)&&lods.insert(id){pending.push(id)}}}}
    // The character writer removes `Root` travel as locomotion. A camera's Root IS its
    // motion: use a private one-to-one lookup key so the writer keeps every position/turn.
    // The exported node still has its exact source name. This changes no values or hierarchy.
    camera.names[0]=crc32(b"__NativeIntroCameraMotion");
    let camera_nodes:Vec<_>=camera_source_hashes.iter().map(|h|{
        let name=file.name(*h).map(str::to_owned).unwrap_or_else(||format!("#{h:08x}"));
        names.add(&name);NodeDef{name:name.clone(),name_hash:camera.names[0],bone_hash:lower33(&name),parent:None,bind:IDENTITY}
    }).collect();
    let camera_object=empty(camera_nodes,vec![]);
    let mut loose:Vec<_>=data.library.meshes.values().filter(|m|m.name.starts_with("optimusv_trans_v2r.")).collect();
    loose.sort_by_key(|m|&m.name);
    let mut loose_nodes=Vec::new();let mut loose_renders=Vec::new();
    for m in &loose {
        assert!(m.skeleton.is_none(),"Unbound source mesh is skinned; refuse invented skeleton mapping");
        let i=loose_nodes.len();
        loose_nodes.push(NodeDef{name:m.name.clone(),name_hash:crc32(m.name.as_bytes()),bone_hash:lower33(&m.name),parent:None,bind:IDENTITY});
        loose_renders.push(RenderDef{node:i,model:m.name.clone(),lod_distances:vec![]});
    }
    let loose_object=empty(loose_nodes,loose_renders);
    let mut remaining:BTreeMap<(&str,Option<u32>),Vec<&tf2_core::formats::model::Mesh>>=BTreeMap::new();
    for(id,m)in &data.library.meshes{
        if original_files.contains_key(id)||m.name.starts_with("optimusv_trans_v2r."){continue}
        remaining.entry((if lods.contains(id){"lod"}else{"ancillary"},m.skeleton)).or_default().push(m);
    }
    let mut collections=Vec::new();
    for((kind,skel),mut meshes)in remaining{
        meshes.sort_by_key(|m|&m.name);
        let bones=skel.map(|id|data.library.skeletons.get(&id).expect("Source mesh skeleton must exist"));
        let stem=format!("{pack_name}_loose_{kind}_{}",skel.map_or_else(||"rigid".to_owned(),|id|format!("skin_{id:08x}")));
        let object=loose_collection(&meshes,bones,&bone_names);
        collections.push((stem,kind,skel,object,meshes.iter().map(|m|lower33(&m.name)).collect::<Vec<_>>()));
    }
    let face_refs:Vec<_>=data.animations.sets.get("FaceSet").into_iter().flatten().filter(|r|matches!(r.id.as_str(),"Parts_Truck2Robot"|"Parts_RobotForm")).collect();
    let face_hashes:BTreeSet<_>=face_refs.iter().filter_map(|r|r.clip.as_ref()).flat_map(|c|c.names.iter().zip(&c.channels).filter(|(_,ch)|ch.keys_any()).map(|(h,_)|*h)).collect();
    assert!(!face_hashes.contains(&anim::ROOT_CHANNEL),"Do not silently drop source Root travel");
    let face_nodes:Vec<_>=face_hashes.iter().map(|h|{let name=names.name(*h);NodeDef{name:name.clone(),name_hash:*h,bone_hash:lower33(&name),parent:None,bind:IDENTITY}}).collect();
    let face_object=empty(face_nodes,vec![]);
    let mut pieces=vec![
        Piece{file:format!("{pack_name}_intro_player"),kind:PieceKind::Robot,object:&data.robot,clips:vec![clip_out("IntroSet.introPlayerAnim",&player)],weapons:vec![]},
        Piece{file:format!("{pack_name}_intro_camera"),kind:PieceKind::Spawner,object:&camera_object,clips:vec![clip_out("IntroSet.introCameraAnim",&camera)],weapons:vec![]},
    ];
    if !loose.is_empty(){pieces.push(Piece{file:format!("{pack_name}_unbound_transform_meshes"),kind:PieceKind::Spawner,object:&loose_object,clips:vec![],weapons:vec![]});}
    if !face_refs.is_empty(){pieces.push(Piece{file:format!("{pack_name}_unbound_face_motion"),kind:PieceKind::Spawner,object:&face_object,clips:face_refs.iter().filter_map(|r|r.clip.as_ref().map(|c|{let mut out=clip_out(&format!("FaceSet.{}",r.id),c);out.looping=r.looping;out.additive=r.additive;out.extras["looping"]=json!(r.looping);out.extras["additive"]=json!(r.additive);out.extras["speed"]=json!(r.speed);out.extras["crossfade"]=json!(r.crossfade);out})).collect(),weapons:vec![]});}
    for(stem,_,_,object,_)in &collections{pieces.push(Piece{file:stem.clone(),kind:PieceKind::Spawner,object,clips:vec![],weapons:vec![]});}
    let out=exported.join(pack_name);
    let mut shared=Shared::new(&data.library,&out).unwrap();
    let mut written=Vec::new();
    for p in &pieces {
        let s=model::write(p.object,&mut shared,&p.clips,&p.file,&names).unwrap();
        assert!(s.problems.is_empty(),"{}: {:?}",p.file,s.problems);
        written.push((format!("{}.glb",p.file),s));
    }
    let roster=tf2_core::formats::characterlist::CharacterList::load(install).unwrap();
    let spec=rotf_export::spec(&roster,pack_name);
    let mut supplemental=rotf_export::describe::character(install,&spec,&data,&pieces,&written,&mut shared,&names);
    // Keep only fields which describe the supplemental files, not duplicate gameplay tables.
    supplemental.as_object_mut().unwrap().retain(|k,_|["format_version","character","space","files","clips","render_sets","materials","collision"].contains(&k.as_str()));
    for f in supplemental["files"].as_array_mut().unwrap(){
        let filename=f["file"].as_str().unwrap().to_owned();
        if filename.contains("_intro_camera") {f["kind"]=json!("camera_track");f["note"]=json!("One original camera motion channel; carrier node has no render geometry. Projection stream is preserved below; no projection semantics inferred.");}
        else if filename.contains("_unbound_transform_meshes") {f["kind"]=json!("unbound_source_meshes");f["note"]=json!("Native vertex coordinates, identity placement, no inferred hierarchy or FaceSet binding. Each named mesh remains separate.");}
        else if filename.contains("_unbound_face_motion") {f["kind"]=json!("unbound_motion_carriers");f["note"]=json!("Independent named motion carriers with identity reference frames, no meshes and no recovered parent hierarchy. Preserves source-local keys for inspection; does not claim an assembled character pose or bind these tracks to loose meshes.");}
        else if filename.contains("_loose_") {f["kind"]=json!("loose_source_collection");f["note"]=json!("Every mesh retains original vertex coordinates and material. Rigid meshes have identity placement; any skin uses independent native inverse-bind-derived rest frames, original weights and palettes. No animation or original parent hierarchy is inferred.");}
        else {f["kind"]=json!("intro_player");}
    }
    supplemental["native_intro_source"]=json!({"player_offset":player_node.offset(),"camera_offset":camera_node.offset(),"camera_channel_hashes":camera_source_hashes,"camera_properties":raw_properties(camera_node),"camera_root_motion":"Native Root position and rotation kept on its one carrier node, rather than discarded as character locomotion.","projection_units":"not established by this export; original float values retained"});
    let(mut native_properties,mut native_events)=(Vec::new(),Vec::new());
    extra_native_data(file.root(),&file,&mut HashSet::new(),&mut native_properties,&mut native_events);
    supplemental["uninterpreted_property_streams"]=json!(native_properties);
    supplemental["uninterpreted_interaction_events"]=json!(native_events);
    let mut unbound=Vec::new();let mut seen=HashSet::new();
    for p in base_pieces.iter().chain(pieces.iter()) {for c in &p.clips {
        let missing:BTreeSet<_>=c.clip.names.iter().zip(&c.clip.channels).filter(|(h,ch)|ch.keys_any()&&**h!=anim::ROOT_CHANNEL&&!p.object.nodes.iter().any(|n|n.name_hash==**h)).map(|(h,_)|*h).collect();
        if missing.is_empty() {continue}
        let channels:Vec<_>=channel_json(c.clip,&names).into_iter().filter(|j|missing.contains(&u32::from_str_radix(j["hash"].as_str().unwrap(),16).unwrap())).collect();
        let signature=json!([c.clip.length,&channels]).to_string();
        if seen.insert(signature){unbound.push(json!({"source_file":format!("{}.glb",p.file),"source_clip":c.name,"length_ticks":c.clip.length,"channels":channels,"note":"Named motion data preserved without inventing missing node bindings; not applied by the viewer."}));}
    }}
    let raw_file=format!("{pack_name}_unbound_animation_channels.json");
    std::fs::write(out.join(&raw_file),serde_json::to_vec_pretty(&json!({"format":"rotf-native-unbound-channels-v1","clip_tick_seconds":1.0/60.0,"coordinate_system":"original game x right y forward z up; quaternion keys as stored","clips":unbound})).unwrap()).unwrap();
    supplemental["unbound_animation_channels_file"]=json!(raw_file);
    supplemental["unbound_animation_channel_groups"]=json!(unbound.len());
    supplemental["unbound_transform_meshes"]=json!(loose.iter().map(|m|&m.name).collect::<Vec<_>>());
    let mut coverage=Vec::new();
    for(id,m)in &data.library.meshes{
        let (file,classification)=if let Some(f)=original_files.get(id){(f.clone(),"original_bound_export")}
        else if m.name.starts_with("optimusv_trans_v2r."){(format!("{pack_name}_unbound_transform_meshes.glb"),"unbound_transform_geometry")}
        else {let(stem,kind,_,_,_)=collections.iter().find(|c|c.4.contains(id)).expect("Every source mesh needs an export");(format!("{stem}.glb"),if *kind=="lod"{"loose_native_lod"}else{"loose_native_ancillary"})};
        coverage.push(json!({"id":format!("{id:08x}"),"name":m.name,"file":file,"classification":classification,"parts":m.parts.len(),"vertices":m.parts.iter().map(|p|p.positions.len()).sum::<usize>(),"skeleton":m.skeleton.map(|s|format!("{s:08x}")),"lods":m.lods.iter().map(|(id,d)|json!({"id":format!("{id:08x}"),"distance":d})).collect::<Vec<_>>()}));
    }
    coverage.sort_by_key(|r|r["name"].as_str().unwrap().to_owned());
    supplemental["source_mesh_coverage"]=json!(coverage);
    supplemental["all_decoded_source_meshes_assigned"]=json!(coverage.len()==data.library.meshes.len());
    // Preserve every source texture, including cubemap faces and all mip levels, under unique IDs.
    let texdir=out.join("source-textures");std::fs::create_dir_all(&texdir).unwrap();
    let mut textures:Vec<_>=data.library.textures.values().collect();textures.sort_by_key(|t|t.id);
    let mut texture_rows=Vec::new();
    for t in textures {
        let dds_name=format!("{:08x}.dds",t.id);let dds=rotf_export::texture::dds(t);
        assert_eq!(&dds[128..],&t.pixels);std::fs::write(texdir.join(&dds_name),&dds).unwrap();
        let png=rotf_export::texture::rgba(t).map(|pixels|{
            let name=format!("{:08x}.png",t.id);
            image::save_buffer(texdir.join(&name),&pixels,t.width,t.height,image::ExtendedColorType::Rgba8).unwrap();
            format!("source-textures/{name}")
        });
        assert!(t.faces==6||png.is_some(),"Flat texture failed decoding");
        texture_rows.push(json!({"id":format!("{:08x}",t.id),"name":t.name,"width":t.width,"height":t.height,"mips":t.mips,"faces":t.faces,"format":format!("{:?}",t.format),"original_pixel_bytes":t.pixels.len(),"dds":format!("source-textures/{dds_name}"),"png":png,"dds_payload_matches_source":true}));
    }
    supplemental["source_textures"]=json!(texture_rows);
    supplemental["source_texture_frames"]=json!(data.library.texture_frames.values().map(|f|json!({"id":format!("{:08x}",f.id),"name":f.name,"frames":f.frames.iter().map(|id|format!("{id:08x}")).collect::<Vec<_>>()})).collect::<Vec<_>>());
    supplemental["library_problems"]=json!(data.library.problems);
    supplemental["write_problems"]=json!(shared.problems);
    supplemental["provenance"]=json!({"tool":"optimus_supplement.rs","license":"MIT","upstream_commit":"dd97296c9a618d06cc2e01bf38c6221b057f6eba","bindings":"IntroPlayer uses native robot Object. IntroCamera has exactly one named native motion channel. Loose meshes use identity placement of their native vertex coordinates; no original attachment is claimed."});
    std::fs::write(out.join(format!("{pack_name}_supplemental.json")),serde_json::to_vec_pretty(&supplemental).unwrap()).unwrap();
    println!("{pack_name}: {} supplemental GLBs, {} loose meshes, {} native textures, {} unbound channel groups",pieces.len(),loose.len(),texture_rows.len(),unbound.len());
    json!({"pack":pack_name,"files":supplemental["files"],"source_textures":texture_rows.len(),"loose_meshes":loose.len(),"unbound_channel_groups":unbound.len(),"problems":shared.problems})
}
fn main(){let args:Vec<_>=std::env::args().skip(1).collect();assert_eq!(args.len(),3,"source-assets exported report.json");let rows:Vec<_>=["optimus","optimusflight","optimusgold"].iter().map(|p|run(Path::new(&args[0]),Path::new(&args[1]),p)).collect();std::fs::write(&args[2],serde_json::to_vec_pretty(&json!({"packs":rows})).unwrap()).unwrap();}
