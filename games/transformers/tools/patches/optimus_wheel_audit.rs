//! Read-only source wheel diagnostics; no guessed placement adjustments.
use tf2_core::formats::{pack::{self,Pack},lxb::{DataFile,Node},hash::lower33};
use glam::{Mat4,Quat,Vec3};
fn main(){
 let dir=tf2_core::install::dir();let data=tf2_core::character::load(dir,"optimus").unwrap();let car=data.vehicle.as_ref().unwrap();
 let pack=Pack::open(&dir.join("characters/optimus.str")).unwrap();let file=DataFile::parse(pack.data(pack.of_type(pack::DATA).next().unwrap()).to_vec()).unwrap();
 let char=file.root().fields().into_iter().flat_map(|(_,r)|r.get("things").into_iter().flat_map(Node::items)).find(|t|t.type_name()==Some("Character")).unwrap();
 let obj=char.path(&["transformModel","vehicleProxy","obj"]).unwrap();
 for a in obj.get("attachments").unwrap().items(){let Some(t)=a.get("thing")else{continue};let ty=t.type_name().unwrap_or("");if !["RenderThing","Collision","SoundPlayer","ParticleEmitter","ParticleEffect","PointLight"].contains(&ty){println!("ATTACH {:?} {} {:?}",a.get("index").and_then(Node::int),ty,t.field_names());}}
 let world:Vec<_>=car.nodes.iter().map(|n|rotf_export::model::matrix(&n.bind)).collect();
 let mut local:Vec<_>=car.nodes.iter().enumerate().map(|(i,n)|n.parent.map_or(world[i],|p|world[p].inverse()*world[i])).collect();
 let rest=local.clone();
 for phase in ["REST","Trans_R2V_A","Trans_R2V_B","V_Idle","REST","Trans_R2V","Trans_R2V_A","Trans_R2V_B","V_Idle"]{
  if phase=="REST"{local.clone_from(&rest);}
  if phase!="REST"{let r=data.vehicle_animations.iter().find(|r|r.id==phase).unwrap();let c=r.clip.as_ref().unwrap();let tick=if phase=="V_Idle"{18.}else{c.length};
   for(i,n)in car.nodes.iter().enumerate(){if let Some(ch)=c.channel(n.name_hash){if let Some((p,q,s))=ch.local(tick,c.length,r.looping){let (_,_,old)=local[i].to_scale_rotation_translation();let q=Quat::from_array(q).conjugate();local[i]=Mat4::from_scale_rotation_translation(Vec3::from(s),if n.name_hash==tf2_core::formats::anim::ROOT_CHANNEL{Quat::IDENTITY}else{q},if n.name_hash==tf2_core::formats::anim::ROOT_CHANNEL{old}else{Vec3::from(p)});}}}
  }
  let mut posed=vec![Mat4::IDENTITY;car.nodes.len()];for(i,n)in car.nodes.iter().enumerate(){posed[i]=n.parent.map_or(local[i],|p|posed[p]*local[i]);}
  println!("PHASE {phase}");
  for(i,n)in car.nodes.iter().enumerate().filter(|(_,n)|n.name.contains("Tire")||n.name.contains("RotatorGoal_04")){
   let(s,r,t)=local[i].to_scale_rotation_translation();println!("NODE {i} {} parent{:?} localT{:?} R{:?} S{:?} world{:?}",n.name,n.parent,t.to_array(),r.to_array(),s.to_array(),posed[i].w_axis.truncate().to_array());
   for render in car.renders.iter().filter(|r|r.node==i){let m=&data.library.meshes[&lower33(&render.model)];let points:Vec<_>=m.parts.iter().flat_map(|p|p.positions.iter()).map(|p|Vec3::from(*p)).collect();let centre=points.iter().copied().sum::<Vec3>()/points.len()as f32;println!("MESH {} localCentre{:?} worldCentre{:?}",m.name,centre.to_array(),posed[i].transform_point3(centre).to_array());}
  }
 }
}
