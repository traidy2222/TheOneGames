//! Local, read-only source coverage audit; never writes or runs the game.
use std::{collections::{BTreeMap, BTreeSet, HashSet}, path::Path};
use serde_json::{Value, json};
use tf2_core::formats::{anim::{self, Clip}, lxb::{DataFile, Node}, pack::{self, Pack}, hash::lower33};

fn signature(c: &Clip) -> String {
    let channels: Vec<_> = c.channels.iter().map(|v| json!([
        v.position.times(), v.position.values(), v.rotation.times(), v.rotation.values(),
        v.scale.times(), v.scale.values(), v.own_scale.times(), v.own_scale.values()
    ])).collect();
    json!({"length":c.length,"start":c.start,"end":c.end,"names":c.names,"channels":channels,
        "model":format!("{:?}",c.model_events),"sound":format!("{:?}",c.sound_events),
        "action":format!("{:?}",c.action_events),"shake":format!("{:?}",c.shake_events),
        "weapon":format!("{:?}",c.weapon_events),"special":format!("{:?}",c.special_fire_events),
        "effect":format!("{:?}",c.effect_events),"trigger":format!("{:?}",c.trigger_events)
    }).to_string()
}

#[derive(Default)]
struct Audit {
    visited: HashSet<(usize, String, Vec<u32>, usize)>,
    native: Vec<Value>, signatures: BTreeSet<String>, orphans: Vec<Value>, failed: Vec<Value>,
    key_classes: BTreeMap<String,usize>, event_classes: BTreeMap<String,usize>,
    unsupported: Vec<Value>, key_mismatches: Vec<Value>, missing_refs: Vec<Value>,
    nodes_visited: usize, objects: Vec<Value>, unsupported_streams: Vec<Value>,
}

fn walk(node: Node<'_>, path: &str, file: &DataFile, represented: &BTreeSet<String>, a: &mut Audit, depth: usize) {
    assert!(depth < 512, "tree exceeds depth limit at {path}");
    let fields = node.fields();
    let ty = node.type_name().unwrap_or("");
    // Offset+type+field schema+array shape keeps anonymous wrapper/first-field identities
    // distinct, and de-duplicates shared typed pointers without depending on their paths.
    if let Some(at) = node.offset() {
        if !a.visited.insert((at, ty.to_owned(), fields.iter().map(|f|f.0).collect(), node.len())) {return}
    }
    a.nodes_visited += 1;
    if ty == "Object" {
        if let Some(obj)=tf2_core::formats::scene::read_object(node,|h|file.name(h)) {
            a.objects.push(json!({"path":path,"offset":node.offset(),"fields":node.field_names(),"node_names":obj.nodes.iter().map(|n|n.name.clone()).collect::<Vec<_>>(),"render_models":obj.renders.iter().map(|r|r.model.clone()).collect::<Vec<_>>()}));
        }
    }
    if ty == "AnimFile" {
        let mut record=json!({"path":path,"offset":node.offset(),"label":node.label().map(|h|file.name(h).map(str::to_owned).unwrap_or_else(||format!("#{h:08x}")))});
        let mut raw_motion_keys=0;
        let mut streams=Vec::new();
        for stream in node.get("streams").into_iter().flat_map(Node::items) {
            let st=stream.get("streamType").and_then(Node::int).unwrap_or(-1);
            for which in ["frame","keys"] {
                if let Some(keys)=stream.get(which) {
                    let class=keys.type_name().unwrap_or("<unnamed>");
                    let count=keys.get("array").map_or(0,Node::len);
                    if class == "<unnamed>" && count == 0 {continue}
                    *a.key_classes.entry(format!("{st:#x}:{which}:{class}")).or_default()+=count;
                    streams.push(json!({"type":st,"storage":which,"class":class,"keys":count}));
                    if ![0,1,2,3,4,5,0x40,0x41,0x42].contains(&st) {
                        let raw:Vec<_>=keys.get("array").into_iter().flat_map(Node::items).map(|k|json!({"index":k.get("index").and_then(Node::int),"time":k.get("time").and_then(Node::float),"float":k.get("value").and_then(Node::float),"floats":k.get("value").map(Node::floats)})).collect();
                        a.unsupported_streams.push(json!({"path":path,"type":st,"storage":which,"class":class,"records":raw}));
                    }
                    match class {
                        "AnimKeysVector3"|"AnimKeysUnsignedShort3"|"AnimKeysVector4"|"AnimKeysUnsigned" => raw_motion_keys+=count,
                        "AnimKeysEvent" => {
                            for key in keys.get("array").into_iter().flat_map(Node::items) {
                                let name=key.get("value").and_then(Node::type_name).unwrap_or("<unnamed>");
                                *a.event_classes.entry(name.to_owned()).or_default()+=1;
                            }
                        }
                        _ => a.unsupported.push(json!({"path":path,"stream_type":st,"storage":which,"class":class,"keys":count})),
                    }
                }
            }
        }
        record["streams"]=json!(streams);
        if let Some(clip)=anim::read_clip(file,node) {
            let sig=signature(&clip);
            let matched=represented.contains(&sig);
            record["length_ticks"]=json!(clip.length);
            record["channels"]=json!(clip.names.len());
            record["represented_by_export_piece"]=json!(matched);
            let decoded:usize=clip.channels.iter().map(|c|c.position.times().len()+c.rotation.times().len()+c.scale.times().len()+c.own_scale.times().len()).sum();
            record["raw_motion_keys"]=json!(raw_motion_keys);
            record["decoded_motion_keys"]=json!(decoded);
            if decoded != raw_motion_keys {a.key_mismatches.push(record.clone());}
            if !matched {a.orphans.push(record.clone());}
            a.signatures.insert(sig);
        } else {a.failed.push(record.clone());}
        a.native.push(record);
    }
    if let Some(anim_file)=node.get("animFile") {
        if anim::read_clip(file,anim_file).is_none() {
            a.missing_refs.push(json!({"path":path,"type":ty,"anim_file_offset":anim_file.offset(),"anim_file_label":anim_file.label(),"anim_id":node.get("animID").and_then(Node::int).map(|id|file.name(id as u32).unwrap_or("?").to_owned())}));
        }
    }
    for (hash,child) in fields {
        let name=file.name(hash).map(str::to_owned).unwrap_or_else(||format!("#{hash:08x}"));
        walk(child,&format!("{path}.{name}"),file,represented,a,depth+1);
    }
    for (i,child) in node.items().enumerate() {walk(child,&format!("{path}[{i}]"),file,represented,a,depth+1);}
}

fn audit(install: &Path, exported: &Path, name: &str) -> Value {
    let path=install.join("characters").join(format!("{name}.str"));
    let pack=Pack::open(&path).unwrap();
    let data=tf2_core::character::load(install,name).unwrap();
    let names=rotf_export::names::Names::load(install,rotf_export::names::data_files(&path),&[]);
    let pieces=rotf_export::pieces(&data,name,&names);
    let represented:BTreeSet<_>=pieces.iter().flat_map(|p|p.clips.iter().map(|c|signature(c.clip))).collect();
    let mut a=Audit::default();
    let mut data_chunks=0;
    for chunk in pack.of_type(pack::DATA) {
        data_chunks+=1;
        a.visited.clear();
        let file=DataFile::parse(pack.data(chunk).to_vec()).unwrap();
        walk(file.root(),&format!("DATA{}",chunk.index),&file,&represented,&mut a,0);
    }
    let exported_only:Vec<_>=represented.difference(&a.signatures).collect();
    let direct:BTreeSet<_>=pieces.iter().flat_map(|p|p.object.renders.iter().map(|r|lower33(&r.model))).collect();
    let mut lods=BTreeSet::new();
    let mut pending:Vec<_>=direct.iter().copied().collect();
    while let Some(id)=pending.pop() {
        if let Some(m)=data.library.meshes.get(&id) {
            for &(id,_) in &m.lods {if !direct.contains(&id) && lods.insert(id) {pending.push(id)}}
        }
    }
    let mut mesh_rows:Vec<_>=data.library.meshes.iter().map(|(id,m)|json!({"id":format!("{id:08x}"),"name":m.name,"parts":m.parts.len(),"vertices":m.parts.iter().map(|p|p.positions.len()).sum::<usize>(),"classification":if direct.contains(id){"export_piece_mesh"}else if lods.contains(id){"lower_detail_mesh_not_exported"}else{"unreferenced_by_export_pieces"}})).collect();
    mesh_rows.sort_by_key(|v|v["name"].as_str().unwrap().to_owned());
    let sidecar:Value=serde_json::from_slice(&std::fs::read(exported.join(name).join(format!("{name}.json"))).unwrap()).unwrap();
    let sideclips=sidecar["clips"].as_array().unwrap();
    let missing_sidecar:Vec<_>=pieces.iter().flat_map(|p|p.clips.iter().filter_map(|c|(!sideclips.iter().any(|j|j["file"]==format!("{}.glb",p.file)&&j["name"]==c.name)).then(||format!("{}:{}",p.file,c.name)))).collect();
    let event_only:Vec<_>=sideclips.iter().filter(|c|c["animation"].is_null()).map(|c|json!({"file":c["file"],"name":c["name"],"events":c["events"].as_array().map_or(0,Vec::len)})).collect();
    let unmatched_channels:Vec<_>=pieces.iter().flat_map(|p|p.clips.iter().filter_map(|c|{
        let nodes:Vec<_>=c.clip.names.iter().zip(&c.clip.channels).filter(|(h,ch)|ch.keys_any()&&!p.object.nodes.iter().any(|n|n.name_hash==**h)&&**h!=anim::ROOT_CHANNEL).map(|(h,_)|names.name(*h)).collect();
        (!nodes.is_empty()).then(||json!({"file":p.file,"clip":c.name,"channels":nodes}))
    })).collect();
    json!({"pack":name,"data_chunks":data_chunks,"nodes_visited":a.nodes_visited,
        "native_anim_file_objects":a.native.len(),"native_unique_decoded_clip_contents":a.signatures.len(),
        "export_piece_clip_references":pieces.iter().map(|p|p.clips.len()).sum::<usize>(),
        "export_unique_clip_contents":represented.len(),"export_only_content_count":exported_only.len(),
        "native_anim_file_decode_failures":a.failed,"native_clips_absent_from_export_pieces":a.orphans,
        "source_anim_refs_without_decodable_file":a.missing_refs,"unsupported_key_classes":a.unsupported,
        "raw_vs_decoded_motion_key_mismatches":a.key_mismatches,"key_classes_record_counts":a.key_classes,
        "raw_event_class_counts":a.event_classes,"library_problems":data.library.problems,"native_objects":a.objects,"unsupported_property_streams":a.unsupported_streams,"export_clip_unmatched_motion_channels":unmatched_channels,
        "mesh_count":mesh_rows.len(),"mesh_rows":mesh_rows,
        "sidecar_clip_records":sideclips.len(),"piece_clips_missing_sidecar":missing_sidecar,"event_only_clips":event_only,
        "native_clips":a.native})
}

fn main() {
    let args:Vec<_>=std::env::args().skip(1).collect();
    assert_eq!(args.len(),3,"source-assets exported audit.json");
    let results:Vec<_>=["optimus","optimusflight","optimusgold"].iter().map(|name|audit(Path::new(&args[0]),Path::new(&args[1]),name)).collect();
    let report=json!({"method":"Read-only walk of every DATA root; unique typed object identities; exact decoded motion plus recognized event content equality against export pieces; raw motion-key count versus decoded-key count. Unreachable bytes outside the typed graph are not inferred as assets. Engine playback and original-game parity are not established by this audit.","upstream_commit":"dd97296c9a618d06cc2e01bf38c6221b057f6eba","packs":results});
    std::fs::write(&args[2],serde_json::to_string_pretty(&report).unwrap()).unwrap();
    for r in report["packs"].as_array().unwrap() {println!("{}: native {} unique {} exported refs {} unique {} orphan {} unsupported {} key mismatches {}",r["pack"],r["native_anim_file_objects"],r["native_unique_decoded_clip_contents"],r["export_piece_clip_references"],r["export_unique_clip_contents"],r["native_clips_absent_from_export_pieces"].as_array().unwrap().len(),r["unsupported_key_classes"].as_array().unwrap().len(),r["raw_vs_decoded_motion_key_mismatches"].as_array().unwrap().len());}
}
