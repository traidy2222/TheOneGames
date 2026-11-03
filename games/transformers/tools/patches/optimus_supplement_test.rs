//! Focused readback of the six newly recovered native introduction clips.
mod common;
use std::{collections::HashMap,path::Path};
use glam::{Mat4,Quat,Vec3};
use gltf::animation::util::ReadOutputs;
use tf2_core::formats::{anim,lxb::{DataFile,Node},pack::{self,Pack}};

fn sample(times:&[f32],values:&[[f32;4]],t:f32,rotation:bool)->[f32;4]{
    let n=times.partition_point(|&v|v<=t);
    if n==0{return values[0]}if n==times.len(){return values[n-1]}
    let k=((t-times[n-1])/(times[n]-times[n-1])).clamp(0.,1.);
    if rotation {Quat::from_array(values[n-1]).slerp(Quat::from_array(values[n]),k).to_array()}
    else {std::array::from_fn(|i|values[n-1][i]+k*(values[n][i]-values[n-1][i]))}
}

#[test]
fn six_native_intro_clips_match_exported_node_motion(){
    let install=tf2_core::install::dir();
    assert!(install.join("characters/optimus.str").is_file(),"source data required; do not silently skip");
    let exported=std::env::var("ROTF_SUPPLEMENT_EXPORT_DIR").expect("ROTF_SUPPLEMENT_EXPORT_DIR required");
    let mut compared=0;
    for name in ["optimus","optimusflight","optimusgold"]{
        let data=tf2_core::character::load(install,name).unwrap();
        let pack=Pack::open(&install.join("characters").join(format!("{name}.str"))).unwrap();
        let chunk=pack.of_type(pack::DATA).next().unwrap();
        let file=DataFile::parse(pack.data(chunk).to_vec()).unwrap();
        let character=file.root().fields().into_iter().flat_map(|(_,r)|r.get("things").into_iter().flat_map(Node::items)).find(|t|t.type_name()==Some("Character")).unwrap();
        for (field,suffix) in [("introPlayerAnim","intro_player"),("introCameraAnim","intro_camera")]{
            let clip=anim::read_clip(&file,character.get(field).unwrap()).unwrap();
            let bytes=std::fs::read(Path::new(&exported).join(name).join(format!("{name}_{suffix}.glb"))).unwrap();
            let gltf=gltf::Gltf::from_slice(&bytes).unwrap();
            let glb=common::Glb{blob:gltf.blob.unwrap(),document:gltf.document};
            assert_eq!(glb.document.animations().count(),1);
            let animation=glb.document.animations().next().unwrap();
            assert_eq!(animation.name(),Some(format!("IntroSet.{field}").as_str()));
            let mut curves=HashMap::new();
            for ch in animation.channels(){
                let reader=ch.reader(|_|Some(&glb.blob));
                let times:Vec<_>=reader.read_inputs().unwrap().collect();
                let (p,values):(u8,Vec<[f32;4]>)=match reader.read_outputs().unwrap(){
                    ReadOutputs::Translations(v)=>(0,v.map(|v|[v[0],v[1],v[2],0.]).collect()),
                    ReadOutputs::Rotations(v)=>(1,v.into_f32().collect()),
                    ReadOutputs::Scales(v)=>(2,v.map(|v|[v[0],v[1],v[2],0.]).collect()),
                    _=>panic!("unexpected animation property"),
                };curves.insert((ch.target().node().index(),p),(times,values));
            }
            let rest=glb.rest();let parents=glb.parents();
            let rig=tf2_core::pose::Rig::new(&data.robot);
            for tick in [0.,(clip.length/3.).floor()+0.5,(clip.length*2./3.).floor(),clip.length]{
                let local=|i:usize|{
                    let(mut t,mut r,mut s)=rest[i];
                    if let Some((ts,vs))=curves.get(&(i,0)){t=Vec3::from_slice(&sample(ts,vs,tick/60.,false)[..3]);}
                    if let Some((ts,vs))=curves.get(&(i,1)){r=Quat::from_array(sample(ts,vs,tick/60.,true));}
                    if let Some((ts,vs))=curves.get(&(i,2)){s=Vec3::from_slice(&sample(ts,vs,tick/60.,false)[..3]);}
                    Mat4::from_scale_rotation_translation(s,r,t)
                };
                let expected:Vec<Mat4>=if suffix=="intro_player"{rig.pose(&clip,false,tick).into_iter().map(Mat4::from).collect()}
                else {
                    assert_eq!(clip.channels.len(),1);
                    let (t,r,s)=clip.channels[0].local(tick,clip.length,false).unwrap();
                    // Source rotations use row vectors; the exporter transposes via conjugation.
                    vec![Mat4::from_scale_rotation_translation(Vec3::from(s),Quat::from_array(r).conjugate(),Vec3::from(t))]
                };
                for(i,e)in expected.iter().enumerate(){
                    let got=glb.world(1+i,&parents,&local);
                    let position=(got.w_axis-e.w_axis).length();
                    let axes=(0..3).map(|k|(got.col(k)-e.col(k)).length()).fold(0f32,f32::max);
                    assert!(position<1e-3+2e-5*e.w_axis.truncate().length(),"{name} {field} tick{tick} node{i} position{position}");
                    assert!(axes<1e-3,"{name} {field} tick{tick} node{i} axes{axes}");
                    compared+=1;
                }
            }
        }
    }
    println!("Six native intro clips: {compared} node poses compared at whole and half ticks");
}

#[test]
fn unbound_carriers_loose_geometry_and_original_texture_payloads_read_back(){
    let install=tf2_core::install::dir();
    let exported=std::env::var("ROTF_SUPPLEMENT_EXPORT_DIR").expect("ROTF_SUPPLEMENT_EXPORT_DIR required");
    let(mut values,mut vertices,mut textures)=(0,0,0);
    for name in ["optimus","optimusflight","optimusgold"]{
        let data=tf2_core::character::load(install,name).unwrap();let out=Path::new(&exported).join(name);
        for t in data.library.textures.values(){
            let dds=std::fs::read(out.join("source-textures").join(format!("{:08x}.dds",t.id))).unwrap();
            assert_eq!(&dds[..4],b"DDS ");assert_eq!(&dds[128..],&t.pixels);textures+=1;
        }
        if name=="optimusflight"{continue}
        let bytes=std::fs::read(out.join(format!("{name}_unbound_transform_meshes.glb"))).unwrap();
        let gltf=gltf::Gltf::from_slice(&bytes).unwrap();let blob=gltf.blob.as_ref().unwrap();
        let meshes:HashMap<_,_>=gltf.meshes().map(|m|(m.name().unwrap(),m)).collect();
        for source in data.library.meshes.values().filter(|m|m.name.starts_with("optimusv_trans_v2r.")){
            for(k,part)in source.parts.iter().enumerate(){
                let key=if source.parts.len()==1{source.name.clone()}else{format!("{}.{k}",source.name)};
                let primitive=meshes[key.as_str()].primitives().next().unwrap();let reader=primitive.reader(|_|Some(blob));
                let positions:Vec<_>=reader.read_positions().unwrap().collect();
                assert_eq!(positions,part.positions,"source vertex coordinates changed in {key}");
                let indices:Vec<_>=reader.read_indices().unwrap().into_u32().collect();
                assert_eq!(indices,part.indices.iter().map(|&v|v as u32).collect::<Vec<_>>());vertices+=positions.len();
            }
        }
        let bytes=std::fs::read(out.join(format!("{name}_unbound_face_motion.glb"))).unwrap();
        let gltf=gltf::Gltf::from_slice(&bytes).unwrap();let blob=gltf.blob.as_ref().unwrap();
        assert_eq!(gltf.meshes().count(),0);assert_eq!(gltf.animations().count(),2);
        for animation in gltf.animations(){
            let id=animation.name().unwrap().strip_prefix("FaceSet.").unwrap();
            let reference=data.animations.sets["FaceSet"].iter().find(|r|r.id==id).unwrap();
            assert!(!reference.additive,"extend check before accepting additive carriers");
            let clip=reference.clip.as_ref().unwrap();
            for ch in animation.channels(){
                let node=ch.target().node();let name=node.name().unwrap();
                let (source_name,own)=name.strip_suffix(".frame").map_or((name,false),|n|(n,true));
                let source=clip.channel(tf2_core::formats::hash::crc32(source_name.as_bytes())).unwrap();
                let reader=ch.reader(|_|Some(blob));let times:Vec<_>=reader.read_inputs().unwrap().collect();
                let (kind,vs):(u8,Vec<[f32;4]>)=match reader.read_outputs().unwrap(){ReadOutputs::Translations(v)=>(0,v.map(|v|[v[0],v[1],v[2],0.]).collect()),ReadOutputs::Rotations(v)=>(1,v.into_f32().collect()),ReadOutputs::Scales(v)=>(2,v.map(|v|[v[0],v[1],v[2],0.]).collect()),_=>panic!()};
                let end=if reference.looping{(clip.length-0.5).max(0.)}else{clip.length};
                for tick in [0.,(clip.length/3.).floor()+0.5,(clip.length*2./3.).floor(),end]{
                    let got=sample(&times,&vs,tick/60.,kind==1);
                    if kind==1 {
                        let q=Quat::from_array(source.rotation.sample(tick,clip.length,reference.looping).unwrap_or([0.,0.,0.,1.])).conjugate().normalize();
                        assert!(1.-Quat::from_array(got).dot(q).abs()<1e-5,"{name} {id} rotation");
                    }else{
                        let track=if kind==0{&source.position}else if own{&source.own_scale}else{&source.scale};
                        let want=track.sample(tick,clip.length,reference.looping).unwrap_or(if kind==0{[0.;3]}else{[1.;3]}).map(|v|if own{v.max(0.001)}else{v});
                        assert!((Vec3::from_slice(&got[..3])-Vec3::from(want)).length()<1e-3,"{name} {id} translation/scale");
                    }
                    values+=1;
                }
            }
        }
    }
    println!("Unbound motion: {values} channel samples; loose geometry: {vertices} exact source vertices; {textures} byte-identical DDS pixel payloads");
}

#[test]
fn every_native_mesh_has_exact_geometry_and_loose_skin_bindings(){
    let install=tf2_core::install::dir();let exported=std::env::var("ROTF_SUPPLEMENT_EXPORT_DIR").unwrap();
    let(mut total,mut vertices,mut skins)=(0,0,0);
    for name in ["optimus","optimusflight","optimusgold"]{
        let data=tf2_core::character::load(install,name).unwrap();let out=Path::new(&exported).join(name);
        let doc:serde_json::Value=serde_json::from_slice(&std::fs::read(out.join(format!("{name}_supplemental.json"))).unwrap()).unwrap();
        let rows=doc["source_mesh_coverage"].as_array().unwrap();assert_eq!(rows.len(),data.library.meshes.len());
        let mut files=HashMap::new();let mut seen=std::collections::HashSet::new();
        for row in rows{
            let id=u32::from_str_radix(row["id"].as_str().unwrap(),16).unwrap();assert!(seen.insert(id));
            let source=&data.library.meshes[&id];let file=row["file"].as_str().unwrap();
            let gltf=files.entry(file.to_owned()).or_insert_with(||gltf::Gltf::from_slice(&std::fs::read(out.join(file)).unwrap()).unwrap());let blob=gltf.blob.as_ref().unwrap();
            for(k,part)in source.parts.iter().enumerate(){
                let key=if source.parts.len()==1{source.name.clone()}else{format!("{}.{k}",source.name)};
                let mesh=gltf.meshes().find(|m|m.name().is_some_and(|n|n.eq_ignore_ascii_case(&key))).unwrap_or_else(||panic!("Missing {name} {key} in {file}"));
                let primitive=mesh.primitives().next().unwrap();let reader=primitive.reader(|_|Some(blob));
                let positions:Vec<_>=reader.read_positions().unwrap().collect();assert_eq!(positions,part.positions);
                let indices:Vec<_>=reader.read_indices().unwrap().into_u32().collect();assert_eq!(indices,part.indices.iter().map(|&v|v as u32).collect::<Vec<_>>());vertices+=positions.len();
                if file.contains("_loose_")&&!part.palette.is_empty(){
                    let node=gltf.nodes().find(|n|n.mesh().is_some_and(|m|m.index()==mesh.index())).unwrap();let skin=node.skin().expect("Native skinned LOD must retain skin");
                    let inverse:Vec<_>=skin.reader(|_|Some(blob)).read_inverse_bind_matrices().unwrap().map(|m|Mat4::from_cols_array_2d(&m)).collect();
                    let bones=&data.library.skeletons[&source.skeleton.unwrap()];
                    assert_eq!(inverse.len(),part.palette.len());
                    for(i,&bone)in part.palette.iter().enumerate(){assert!(inverse[i].abs_diff_eq(rotf_export::model::matrix(&bones[bone as usize].inverse_bind),1e-6));}
                    let joints:Vec<_>=reader.read_joints(0).unwrap().into_u16().collect();let weights:Vec<_>=reader.read_weights(0).unwrap().into_f32().collect();
                    assert_eq!(joints.len(),positions.len());assert_eq!(weights.len(),positions.len());
                    for(i,(j,w))in joints.iter().zip(&weights).enumerate(){
                        let raw=part.blend_weights[i].map(|v|v.max(0.));let sum: f32=raw.iter().sum();let expected=if sum>0.{raw.map(|v|v/sum)}else{[1.,0.,0.,0.]};
                        for k in 0..4{if expected[k]>0.{assert_eq!(j[k],part.blend_indices[i][k] as u16);}assert!((w[k]-expected[k]).abs()<1e-6);}
                    }
                    skins+=1;
                }
            }
            total+=1;
        }
    }
    println!("Every source mesh: {total} IDs, {vertices} exact source vertices; {skins} loose skinned parts preserve original palettes, inverse binds and normalized weights");
}
